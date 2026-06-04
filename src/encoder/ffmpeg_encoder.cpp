#include "ffmpeg_encoder.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

/* av_err2str macro does not work in C++, use a helper function instead */
static char err_buf[AV_ERROR_MAX_STRING_SIZE] = {};
static const char *av_err2str_c(int errnum) {
    return av_make_error_string(err_buf, AV_ERROR_MAX_STRING_SIZE, errnum);
}

FFmpegEncoder::FFmpegEncoder() = default;

FFmpegEncoder::~FFmpegEncoder() {
    close();
}

void FFmpegEncoder::open(const Config &cfg) {
    cfg_ = cfg;

    /* Find encoder: try RKMPP first, fallback to software */
    const AVCodec *codec = nullptr;
    if (cfg.codec == MJPEG) {
        codec = avcodec_find_encoder_by_name("mjpeg_rkmpp");
        if (!codec) {
            fprintf(stderr, "mjpeg_rkmpp not found, trying mjpeg\n");
            codec = avcodec_find_encoder_by_name("mjpeg");
        }
        if (!codec)
            throw std::runtime_error("No MJPEG encoder found");
    } else if (cfg.codec == H265) {
        codec = avcodec_find_encoder_by_name("hevc_rkmpp");
        if (!codec) {
            fprintf(stderr, "hevc_rkmpp not found, trying libx265\n");
            codec = avcodec_find_encoder_by_name("libx265");
        }
        if (!codec)
            throw std::runtime_error("No H265/HEVC encoder found");
    } else {
        codec = avcodec_find_encoder_by_name("h264_rkmpp");
        if (!codec) {
            fprintf(stderr, "h264_rkmpp not found, trying libx264\n");
            codec = avcodec_find_encoder_by_name("libx264");
        }
        if (!codec)
            throw std::runtime_error("No H264 encoder found");
    }
    is_rkmpp_ = (strstr(codec->name, "rkmpp") != nullptr);
    printf("Using encoder: %s\n", codec->name);

    /* Allocate context */
    enc_ctx_ = avcodec_alloc_context3(codec);
    if (!enc_ctx_)
        throw std::runtime_error("Could not allocate encoder context");

    enc_ctx_->bit_rate  = cfg.bitrate;
    enc_ctx_->width     = cfg.width;
    enc_ctx_->height    = cfg.height;
    enc_ctx_->time_base = (AVRational){ 1, cfg.fps };
    enc_ctx_->framerate = (AVRational){ cfg.fps, 1 };
    enc_ctx_->gop_size  = cfg.gop_size;
    if (cfg.codec == MJPEG)
        enc_ctx_->gop_size = 1;  /* MJPEG: every frame is keyframe */
    enc_ctx_->max_b_frames = cfg.b_frames;
    if (cfg.codec == MJPEG)
        enc_ctx_->max_b_frames = 0;
    enc_ctx_->pix_fmt   = AV_PIX_FMT_NV12;

    printf("Encoder config: %dx%d, bitrate=%ld, fps=%d, gop=%d, b_frames=%d, pix_fmt=NV12\n",
           enc_ctx_->width, enc_ctx_->height, enc_ctx_->bit_rate,
           cfg.fps, enc_ctx_->gop_size, enc_ctx_->max_b_frames);

    if (is_rkmpp_)
    {
        if (cfg.codec == MJPEG) {
            if (av_opt_set(enc_ctx_->priv_data, "rc_mode", "CBR", 0) < 0)
                fprintf(stderr, "Warning: failed to set rc_mode=CBR\n");
            else
                printf("Using RKMPP MJPEG encoder with CBR mode\n");
        } else {
            if (av_opt_set(enc_ctx_->priv_data, "rc_mode", "VBR", 0) < 0)
                fprintf(stderr, "Warning: failed to set rc_mode=VBR\n");
            else
                printf("Using RKMPP encoder with VBR mode\n");
        }
    }
    else {
        enc_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        if (cfg.low_latency) {
            if (av_opt_set(enc_ctx_->priv_data, "preset",  "ultrafast", 0) < 0)
                fprintf(stderr, "Warning: failed to set preset=ultrafast\n");
            if (av_opt_set(enc_ctx_->priv_data, "tune",    "zerolatency", 0) < 0)
                fprintf(stderr, "Warning: failed to set tune=zerolatency\n");
            enc_ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
            enc_ctx_->flags2 |= AV_CODEC_FLAG2_FAST;
        } else {
            if (av_opt_set(enc_ctx_->priv_data, "preset", "medium", 0) < 0)
                fprintf(stderr, "Warning: failed to set preset=medium\n");
        }
    }

    int ret = avcodec_open2(enc_ctx_, codec, nullptr);
    if (ret < 0)
        throw std::runtime_error(std::string("Could not open codec: ") + av_err2str_c(ret));

    /* Allocate frame */
    frame_ = av_frame_alloc();
    frame_->format = enc_ctx_->pix_fmt;
    frame_->width  = cfg.width;
    frame_->height = cfg.height;
    ret = av_frame_get_buffer(frame_, 0);
    if (ret < 0)
        throw std::runtime_error(std::string("Could not allocate frame: ") + av_err2str_c(ret));

    pkt_ = av_packet_alloc();

    /* Setup NV12->YUV420P converter for libx264/libx265 path */
    if (!is_rkmpp_) {
        sws_ = sws_getContext(cfg.width, cfg.height, AV_PIX_FMT_NV12,
                              cfg.width, cfg.height, AV_PIX_FMT_YUV420P,
                              0, nullptr, nullptr, nullptr);
        frame_yuv_ = av_frame_alloc();
        frame_yuv_->format = AV_PIX_FMT_YUV420P;
        frame_yuv_->width  = cfg.width;
        frame_yuv_->height = cfg.height;
        av_frame_get_buffer(frame_yuv_, 0);
    }
}

void FFmpegEncoder::fill_nv12_frame(AVFrame *frame, const void *data, int width, int height) {
    int y_size = width * height;
    memcpy(frame->data[0], data, y_size);
    memcpy(frame->data[1], static_cast<const uint8_t *>(data) + y_size, y_size / 2);
}

void FFmpegEncoder::do_encode(AVFrame *frame, PacketCallback &cb) {
    int ret = avcodec_send_frame(enc_ctx_, frame);
    if (ret < 0) {
        fprintf(stderr, "avcodec_send_frame: %s\n", av_err2str_c(ret));
        return;
    }
    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx_, pkt_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        if (ret < 0) {
            fprintf(stderr, "avcodec_receive_packet: %s\n", av_err2str_c(ret));
            return;
        }
        if (cb)
            cb(pkt_);
        av_packet_unref(pkt_);
    }
}

void FFmpegEncoder::encode_with_packets(const void *data, int64_t pts, PacketCallback cb) {
    if (is_rkmpp_) {
        av_frame_make_writable(frame_);
        fill_nv12_frame(frame_, data, cfg_.width, cfg_.height);
        frame_->pts = pts;
        do_encode(frame_, cb);
    } else {
        /* NV12 -> YUV420P conversion */
        av_frame_make_writable(frame_yuv_);
        frame_->data[0]     = static_cast<uint8_t *>(const_cast<void *>(data));
        frame_->data[1]     = static_cast<uint8_t *>(const_cast<void *>(data)) + cfg_.width * cfg_.height;
        frame_->linesize[0] = cfg_.width;
        frame_->linesize[1] = cfg_.width;
        sws_scale(sws_,
                  const_cast<const uint8_t *const *>(frame_->data), frame_->linesize,
                  0, cfg_.height,
                  frame_yuv_->data, frame_yuv_->linesize);
        frame_yuv_->pts = pts;
        do_encode(frame_yuv_, cb);
    }
}

void FFmpegEncoder::flush() {
    printf("Flushing encoder...\n");
    PacketCallback noop;
    do_encode(nullptr, noop);
}

AVPixelFormat FFmpegEncoder::pix_fmt() const {
    return enc_ctx_->pix_fmt;
}

const AVCodecContext *FFmpegEncoder::codec_ctx() const {
    return enc_ctx_;
}

void FFmpegEncoder::close() {
    if (sws_)        { sws_freeContext(sws_); sws_ = nullptr; }
    if (frame_yuv_)  { av_frame_free(&frame_yuv_); }
    if (frame_)      { av_frame_free(&frame_); }
    if (pkt_)        { av_packet_free(&pkt_); }
    if (enc_ctx_)    { avcodec_free_context(&enc_ctx_); }
}
