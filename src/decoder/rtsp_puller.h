#pragma once

#include "node.h"
#include "blocking_queue.h"
#include "common.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

class RTSPPullerNode : public NodeBase {
public:
    struct Config {
        const char *url       = "rtsp://localhost:8554/live";
        const char *transport = "tcp";   /* tcp or udp */
        int         timeout   = 5000000; /* microseconds */
    };

    explicit RTSPPullerNode(const Config &cfg) : cfg_(cfg) {}

    std::string name() const override { return "RTSPPuller"; }
    void init() override;
    void run() override;
    void stop() override;

    void set_output(BlockingQueue<Packet> *q) { output_ = q; }

    /* Available after init() */
    int width()  const { return width_; }
    int height() const { return height_; }
    const AVCodecParameters *codec_params() const { return codec_params_; }

private:
    Config                     cfg_;
    AVFormatContext           *fmt_ctx_     = nullptr;
    const AVCodec             *codec_       = nullptr;
    AVCodecContext            *codec_ctx_   = nullptr;
    int                        video_idx_   = -1;
    int                        width_       = 0;
    int                        height_      = 0;
    const AVCodecParameters   *codec_params_ = nullptr;
    BlockingQueue<Packet>     *output_      = nullptr;
};
