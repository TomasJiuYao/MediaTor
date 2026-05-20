#include "ffmpeg_encoder_node.h"

#include <cstdio>

void FFmpegEncoderNode::init() {
    FFmpegEncoder::Config enc_cfg;
    enc_cfg.width       = cfg_.width;
    enc_cfg.height      = cfg_.height;
    enc_cfg.fps         = cfg_.fps;
    enc_cfg.bitrate     = cfg_.bitrate;
    enc_cfg.gop_size    = cfg_.gop_size;
    enc_cfg.b_frames    = cfg_.b_frames;
    enc_cfg.codec       = cfg_.codec;
    enc_cfg.low_latency = cfg_.low_latency;
    enc_.open(enc_cfg);
}

void FFmpegEncoderNode::run() {
    printf("[FFmpegEncoder] running\n");

    while (running_.load()) {
        Frame frame;
        if (!input_->pop(frame))
            break; /* queue closed */

        /* Encode the frame; for each output packet, push to all downstream queues */
        enc_.encode_with_packets(frame.data, frame.pts,
            [this](const AVPacket *pkt) {
                for (auto *out : outputs_) {
                    Packet packet(pkt);
                    if (!out->push(std::move(packet))) {
                        /* downstream queue closed, skip */
                    }
                }
            });

        /* Frame destructor returns V4L2 buffer via release callback */
    }

    /* Flush encoder */
    enc_.flush();
    printf("[FFmpegEncoder] done\n");
}
