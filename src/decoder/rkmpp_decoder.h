#pragma once

#include "node.h"
#include "blocking_queue.h"
#include "common.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
}

class RKMPPDecoderNode : public NodeBase {
public:
    enum Codec { H264, H265, MJPEG };

    struct Config {
        Codec       codec      = H264;
        const char *drm_device = "/dev/dri/card0";
        int         width      = 0;
        int         height     = 0;
    };

    explicit RKMPPDecoderNode(const Config &cfg) : cfg_(cfg) {}

    std::string name() const override { return "RKMPPDecoder"; }
    void init() override;
    void run() override;
    void stop() override;

    void set_input(BlockingQueue<Packet> *q)   { input_ = q; }
    void add_output(BlockingQueue<DecodedFrame> *q) { outputs_.push_back(q); }

    /* Set codec params from puller — call before init() */
    void set_codec_params(const AVCodecParameters *params) { codec_params_ = params; }

private:
    Config                          cfg_;
    const AVCodecParameters        *codec_params_ = nullptr;
    AVCodecContext                 *dec_ctx_      = nullptr;
    AVBufferRef                    *hw_dev_ctx_   = nullptr;
    BlockingQueue<Packet>          *input_        = nullptr;
    std::vector<BlockingQueue<DecodedFrame>*> outputs_;
};
