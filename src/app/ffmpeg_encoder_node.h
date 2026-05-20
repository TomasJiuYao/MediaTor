#pragma once

#include "node.h"
#include "blocking_queue.h"
#include "common.h"
#include "ffmpeg_encoder.h"

class FFmpegEncoderNode : public NodeBase {
public:
    struct Config {
        int      width     = 1920;
        int      height    = 1080;
        int      fps       = 30;
        int      bitrate   = 2000000;
        int      gop_size  = 30;
        int      b_frames  = 0;
        FFmpegEncoder::Codec codec = FFmpegEncoder::H264;
        bool     low_latency = true;
    };

    explicit FFmpegEncoderNode(const Config &cfg) : cfg_(cfg) {}

    std::string name() const override { return "FFmpegEncoder"; }
    void init() override;
    void run() override;

    void set_input(BlockingQueue<Frame> *q)  { input_ = q; }
    void add_output(BlockingQueue<Packet> *q) { outputs_.push_back(q); }

    /* Update config before init() — e.g. propagate capture resolution */
    void set_resolution(int w, int h) { cfg_.width = w; cfg_.height = h; }

    const AVCodecContext *codec_ctx() const { return enc_.codec_ctx(); }

private:
    Config                          cfg_;
    FFmpegEncoder                   enc_;
    BlockingQueue<Frame>           *input_ = nullptr;
    std::vector<BlockingQueue<Packet>*> outputs_;
};
