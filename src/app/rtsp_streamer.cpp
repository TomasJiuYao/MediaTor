#include "rtsp_streamer.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/avutil.h>
}

static char err_buf[AV_ERROR_MAX_STRING_SIZE] = {};
static const char *av_err2str_c(int errnum) {
    return av_make_error_string(err_buf, AV_ERROR_MAX_STRING_SIZE, errnum);
}

RTSPStreamer::RTSPStreamer() = default;

RTSPStreamer::~RTSPStreamer() {
    close();
}

void RTSPStreamer::open(const Config &cfg, const AVCodecContext *codec_ctx) {
    int ret;

    /* Allocate output format context — RTSP uses rtp muxer */
    ret = avformat_alloc_output_context2(&fmt_ctx_, nullptr, "rtsp", cfg.url);
    if (ret < 0 || !fmt_ctx_)
        throw std::runtime_error(std::string("Could not alloc RTSP output context: ") + av_err2str_c(ret));

    /* ---- Low-latency RTSP muxer options ---- */
    /* Flush packets immediately, no buffering */
    av_opt_set(fmt_ctx_->priv_data, "flush_packets", "1", 0);
    /* Use UDP for lower latency (TCP adds framing overhead + head-of-line blocking) */
    av_opt_set(fmt_ctx_->priv_data, "rtsp_transport", "udp", 0);
    /* Minimize UDP packetization latency (ms) — send partial packets sooner */
    av_opt_set(fmt_ctx_->priv_data, "max_delay", "0", 0);
    /* No AVIO buffering — write to network as fast as possible */
    av_opt_set(fmt_ctx_->priv_data, "fflags", "+nobuffer", 0);

    /* Create video stream and copy encoder parameters */
    stream_ = avformat_new_stream(fmt_ctx_, nullptr);
    if (!stream_)
        throw std::runtime_error("Could not create RTSP video stream");

    ret = avcodec_parameters_from_context(stream_->codecpar, codec_ctx);
    if (ret < 0)
        throw std::runtime_error(std::string("Could not copy codec params: ") + av_err2str_c(ret));

    /* Use standard RTP time_base (1/90000) for H264/H265 payload.
     * Encoder time_base may differ, but rescale in write_packet. */
    stream_->time_base = (AVRational){ 1, 90000 };

    /* Open RTSP connection — ANNOUNCE/SETUP/RECORD handshake */
    if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
        AVDictionary *opts = nullptr;
        /* TCP fallback if UDP blocked — but prefer UDP for latency */
        av_dict_set(&opts, "rtsp_transport", "udp", 0);
        /* Connection timeout (microseconds) */
        av_dict_set(&opts, "stimeout", "5000000", 0);

        ret = avio_open2(&fmt_ctx_->pb, cfg.url, AVIO_FLAG_WRITE, nullptr, &opts);
        av_dict_free(&opts);
        if (ret < 0)
            throw std::runtime_error(std::string("Could not open RTSP URL '") + cfg.url + "': " + av_err2str_c(ret));
    }

    ret = avformat_write_header(fmt_ctx_, nullptr);
    if (ret < 0)
        throw std::runtime_error(std::string("Could not write RTSP header: ") + av_err2str_c(ret));

    opened_ = true;
    printf("RTSP: streaming to %s (udp, low-latency)\n", cfg.url);
}

void RTSPStreamer::write_packet(const AVPacket *pkt) {
    if (!opened_)
        return;

    AVPacket tmp;
    av_packet_ref(&tmp, pkt);

    /* Rescale from encoder time_base to RTP 1/90000 */
    av_packet_rescale_ts(&tmp, pkt->time_base, stream_->time_base);
    tmp.stream_index = stream_->index;

    /* Use av_write_frame (non-interleaved) instead of av_interleaved_write_frame.
     * Interleaved mode re-orders packets which adds latency and is only needed
     * for multi-stream muxing (audio+video). Video-only = no reordering needed. */
    int ret = av_write_frame(fmt_ctx_, &tmp);
    av_packet_unref(&tmp);

    if (ret < 0)
        fprintf(stderr, "RTSP: write_frame error: %s\n", av_err2str_c(ret));
}

void RTSPStreamer::close() {
    if (!opened_)
        return;

    av_write_trailer(fmt_ctx_);

    if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE) && fmt_ctx_->pb)
        avio_closep(&fmt_ctx_->pb);

    avformat_free_context(fmt_ctx_);
    fmt_ctx_ = nullptr;
    opened_  = false;
    printf("RTSP: stream closed\n");
}
