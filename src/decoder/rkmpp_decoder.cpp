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
    const char *codec_name = nullptr;
    if (cfg_.codec == H265)
        codec_name = "hevc_rkmpp";
    else if (cfg_.codec == MJPEG)
        codec_name = "mjpeg_rkmpp";
    else
        codec_name = "h264_rkmpp";

    const AVCodec *codec = avcodec_find_decoder_by_name(codec_name);
    if (!codec) {
        fprintf(stderr, "[RKMPPDecoder] %s not found, falling back to software\n", codec_name);
        if (cfg_.codec == H265)
            codec_name = "hevc";
        else if (cfg_.codec == MJPEG)
            codec_name = "mjpeg";
        else
            codec_name = "h264";
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

    /* Set dimensions from config if no codec_params_ */
    if (!codec_params_) {
        if (cfg_.width > 0 && cfg_.height > 0) {
            dec_ctx_->width  = cfg_.width;
            dec_ctx_->height = cfg_.height;
        }
    }

    bool is_rkmpp = (strstr(codec->name, "rkmpp") != nullptr);

    /* get_format callback: request NV12 output from RKMPP decoder.
     * Without this, the decoder may output DRM_PRIME frames that
     * cannot be properly allocated, causing "invalid buffer size 0". */
    if (is_rkmpp) {
        dec_ctx_->get_format = [](AVCodecContext *ctx,
                                  const enum AVPixelFormat *fmts) -> enum AVPixelFormat {
            for (const enum AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; p++) {
                if (*p == AV_PIX_FMT_NV12)
                    return AV_PIX_FMT_NV12;
            }
            return AV_PIX_FMT_NONE;
        };
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
