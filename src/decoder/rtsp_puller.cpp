#include "rtsp_puller.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

extern "C" {
#include <libavutil/opt.h>
}

static char pull_err_buf[AV_ERROR_MAX_STRING_SIZE] = {};
static const char *pull_av_err2str(int errnum) {
    return av_make_error_string(pull_err_buf, AV_ERROR_MAX_STRING_SIZE, errnum);
}

void RTSPPullerNode::init() {
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", cfg_.transport, 0);
    av_dict_set(&opts, "stimeout", std::to_string(cfg_.timeout).c_str(), 0);
    av_dict_set(&opts, "fflags", "nobuffer", 0);
    av_dict_set(&opts, "allowed_media_types", "video", 0);

    int ret = avformat_open_input(&fmt_ctx_, cfg_.url, nullptr, &opts);
    av_dict_free(&opts);
    if (ret < 0)
        throw std::runtime_error(std::string("RTSP pull: could not open '") + cfg_.url + "': " + pull_av_err2str(ret));

    ret = avformat_find_stream_info(fmt_ctx_, nullptr);
    if (ret < 0)
        throw std::runtime_error(std::string("RTSP pull: could not find stream info: ") + pull_av_err2str(ret));

    /* Find video stream */
    video_idx_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, &codec_, 0);
    if (video_idx_ < 0)
        throw std::runtime_error("RTSP pull: no video stream found");

    printf("[RTSPPuller] video stream %d: %s %dx%d\n",
           video_idx_, codec_->name,
           fmt_ctx_->streams[video_idx_]->codecpar->width,
           fmt_ctx_->streams[video_idx_]->codecpar->height);

    width_  = fmt_ctx_->streams[video_idx_]->codecpar->width;
    height_ = fmt_ctx_->streams[video_idx_]->codecpar->height;
    codec_params_ = fmt_ctx_->streams[video_idx_]->codecpar;
}

void RTSPPullerNode::run() {
    printf("[RTSPPuller] running, pulling from %s\n", cfg_.url);

    while (running_.load()) {
        AVPacket *pkt = av_packet_alloc();
        int ret = av_read_frame(fmt_ctx_, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                printf("[RTSPPuller] EOF reached\n");
            } else {
                fprintf(stderr, "[RTSPPuller] read error: %s\n", pull_av_err2str(ret));
            }
            av_packet_free(&pkt);
            break;
        }

        if (pkt->stream_index == video_idx_) {
            /* Pass packet with original stream time_base; decoder handles rescaling */

            Packet out_pkt(pkt);
            if (!output_->push(std::move(out_pkt))) {
                av_packet_free(&pkt);
                break; /* queue closed */
            }
        }
        av_packet_free(&pkt);
    }

    printf("[RTSPPuller] done\n");
}

void RTSPPullerNode::stop() {
    NodeBase::stop();
    if (fmt_ctx_) {
        avformat_close_input(&fmt_ctx_);
    }
}
