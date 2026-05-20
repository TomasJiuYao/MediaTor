#pragma once

#include "node.h"
#include "blocking_queue.h"
#include "common.h"
#include "rtsp_streamer.h"

class RTSPStreamerNode : public NodeBase {
public:
    struct Config {
        const char *url = "rtsp://localhost:8554/test";
    };

    explicit RTSPStreamerNode(const Config &cfg) : cfg_(cfg) {}

    std::string name() const override { return "RTSPStreamer"; }
    void init() override; /* no-op: RTSP opened lazily in run() once codec_ctx is set */
    void run() override;

    void set_input(BlockingQueue<Packet> *q) { input_ = q; }

    /* Must be called after FFmpegEncoderNode::init() to get codec_ctx */
    void set_codec_ctx(const AVCodecContext *ctx) { codec_ctx_ = ctx; }

private:
    Config                   cfg_;
    const AVCodecContext    *codec_ctx_ = nullptr;
    RTSPStreamer             rtsp_;
    BlockingQueue<Packet>   *input_ = nullptr;
};
