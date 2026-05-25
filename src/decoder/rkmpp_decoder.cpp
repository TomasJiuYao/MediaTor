#include "rkmpp_decoder.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/hwcontext_rkmpp.h>
}

static char dec_err_buf[AV_ERROR_MAX_STRING_SIZE] = {};
static const char *dec_av_err2str(int errnum) {
    return av_make_error_string(dec_err_buf, AV_ERROR_MAX_STRING_SIZE, errnum);
}

void RKMPPDecoderNode::init() {
    /* Find RKMPP decoder */
    const char *codec_name = (cfg_.codec == H265) ? "hevc_rkmpp" : "h264_rkmpp";
    const AVCodec *codec = avcodec_find_decoder_by_name(codec_name);
    if (!codec) {
        fprintf(stderr, "[RKMPPDecoder] %s not found, falling back to software\n", codec_name);
        codec_name = (cfg_.codec == H265) ? "hevc" : "h264";
        codec = avcodec_find_decoder_by_name(codec_name);
    }
    if (!codec)
        throw std::runtime_error(std::string("No decoder found for ") + codec_name);

    printf("[RKMPPDecoder] using decoder: %s\n", codec->name);

    dec_ctx_ = avcodec_alloc_context3(codec);
    if (!dec_ctx_)
        throw std::runtime_error("Could not allocate decoder context");

    /* Copy codec params from RTSP puller */
    if (codec_params_) {
        int ret = avcodec_parameters_to_context(dec_ctx_, codec_params_);
        if (ret < 0)
            throw std::runtime_error(std::string("Could not copy codec params: ") + dec_av_err2str(ret));
    }

    /* Setup DRM/RKMPP hardware device context */
    bool is_rkmpp = (strstr(codec->name, "rkmpp") != nullptr);
    if (is_rkmpp) {
        int ret = av_hwdevice_ctx_create(&hw_dev_ctx_, AV_HWDEVICE_TYPE_DRM,
                                          cfg_.drm_device, nullptr, 0);
        if (ret < 0) {
            fprintf(stderr, "[RKMPPDecoder] could not create DRM hw device: %s\n", dec_av_err2str(ret));
        } else {
            dec_ctx_->hw_device_ctx = av_buffer_ref(hw_dev_ctx_);
            printf("[RKMPPDecoder] DRM hw device ctx created on %s\n", cfg_.drm_device);
        }
    }

    dec_ctx_->thread_count = 1;

    int ret = avcodec_open2(dec_ctx_, codec, nullptr);
    if (ret < 0)
        throw std::runtime_error(std::string("Could not open decoder: ") + dec_av_err2str(ret));

    printf("[RKMPPDecoder] opened: %dx%d\n", dec_ctx_->width, dec_ctx_->height);
}

void RKMPPDecoderNode::run() {
    printf("[RKMPPDecoder] running\n");

    while (running_.load()) {
        Packet pkt;
        if (!input_->pop(pkt))
            break; /* queue closed */

        int ret = avcodec_send_packet(dec_ctx_, pkt.pkt);
        if (ret < 0) {
            fprintf(stderr, "[RKMPPDecoder] send_packet error: %s\n", dec_av_err2str(ret));
            continue;
        }

        while (ret >= 0) {
            AVFrame *frame = av_frame_alloc();
            ret = avcodec_receive_frame(dec_ctx_, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                av_frame_free(&frame);
                break;
            }
            if (ret < 0) {
                fprintf(stderr, "[RKMPPDecoder] receive_frame error: %s\n", dec_av_err2str(ret));
                av_frame_free(&frame);
                break;
            }

            DecodedFrame df(frame);
            for (auto *out : outputs_) {
                /* Each output needs its own frame ref */
                if (out == outputs_.back()) {
                    out->push(std::move(df));
                } else {
                    AVFrame *dup = av_frame_alloc();
                    av_frame_ref(dup, frame);
                    out->push(DecodedFrame(dup));
                }
            }
        }
    }

    /* Flush decoder */
    avcodec_send_packet(dec_ctx_, nullptr);
    printf("[RKMPPDecoder] done\n");
}

void RKMPPDecoderNode::stop() {
    NodeBase::stop();
}
