#include "rtsp_streamer_node.h"

#include <cstdio>
#include <stdexcept>

void RTSPStreamerNode::init() {
    /* No-op: RTSP is opened in run() after codec_ctx is set */
}

void RTSPStreamerNode::run() {
    /* Open RTSP connection now that codec_ctx is available */
    if (!codec_ctx_) {
        fprintf(stderr, "[RTSPStreamer] no codec_ctx, skipping\n");
        return;
    }

    RTSPStreamer::Config rtsp_cfg;
    rtsp_cfg.url = cfg_.url;
    rtsp_.open(rtsp_cfg, codec_ctx_);

    printf("[RTSPStreamer] running\n");

    while (running_.load()) {
        Packet packet;
        if (!input_->pop(packet))
            break; /* queue closed */

        if (packet.pkt)
            rtsp_.write_packet(packet.pkt);
    }

    rtsp_.close();
    printf("[RTSPStreamer] done\n");
}
