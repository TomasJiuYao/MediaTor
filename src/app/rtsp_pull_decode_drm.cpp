/*
 * RTSP pull -> RKMPP decode -> DRM display
 *
 * Pipeline:
 *   RTSPPullerNode --[Packet]--> RKMPPDecoderNode --[DecodedFrame]--> DRMDisplayNode
 *
 * Usage:
 *   ./rtsp_pull_decode_drm [rtsp_url] [codec] [drm_device]
 *   Defaults: rtsp://192.168.42.110:8554/live, h264, /dev/card0
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "rtsp_puller.h"
#include "rkmpp_decoder.h"
#include "drm_display.h"
#include "pipeline.h"
#include "blocking_queue.h"
#include "common.h"

static Pipeline *g_pipeline = nullptr;

static void signal_handler(int) {
    if (g_pipeline) g_pipeline->stop();
}

int main(int argc, char **argv)
{
    std::cout << "RTSP Pull -> RKMPP Decode -> DRM Display\n"
              << "\n"
              << "Usage: rtsp_pull_decode_drm [rtsp_url] [codec] [drm_device]\n"
              << "\n"
              << "Arguments:\n"
              << "  rtsp_url    RTSP pull URL            (default: rtsp://192.168.5.100:8554/live)\n"
              << "  codec       h264|h265                (default: h264)\n"
              << "  drm_device  DRM device path           (default: /dev/card0)\n"
              << std::endl;

    const char *rtsp_url   = (argc > 1) ? argv[1] : "rtsp://192.168.5.100:8554/live";
    const char *codec_str  = (argc > 2) ? argv[2] : "h264";
    const char *drm_device = (argc > 3) ? argv[3] : "/dev/dri/card0";

    RKMPPDecoderNode::Codec codec_type = RKMPPDecoderNode::H264;
    if (strcmp(codec_str, "h265") == 0 || strcmp(codec_str, "hevc") == 0) {
        codec_type = RKMPPDecoderNode::H265;
    }

    std::cout << "RTSP URL: " << rtsp_url
              << ", Codec: " << (codec_type == RKMPPDecoderNode::H265 ? "H265" : "H264")
              << ", DRM: " << drm_device
              << std::endl;

    try {
        /* Create queues */
        BlockingQueue<Packet>       pull_to_dec(8);
        BlockingQueue<DecodedFrame> dec_to_drm(4);

        /* Create nodes */
        RTSPPullerNode::Config pull_cfg;
        pull_cfg.url = rtsp_url;

        RKMPPDecoderNode::Config dec_cfg;
        dec_cfg.codec      = codec_type;
        dec_cfg.drm_device = drm_device;

        DRMDisplayNode::Config drm_cfg;
        drm_cfg.device     = drm_device;
        drm_cfg.plane_type = DRM_PLANE_TYPE_PRIMARY;

        auto pull_node = std::make_shared<RTSPPullerNode>(pull_cfg);
        auto dec_node  = std::make_shared<RKMPPDecoderNode>(dec_cfg);
        auto drm_node  = std::make_shared<DRMDisplayNode>(drm_cfg);

        /* Wire up queues */
        pull_node->set_output(&pull_to_dec);
        dec_node->set_input(&pull_to_dec);
        dec_node->add_output(&dec_to_drm);
        drm_node->set_input(&dec_to_drm);

        /* Build pipeline */
        Pipeline pipeline;
        pipeline.add_node(pull_node);
        pipeline.add_node(dec_node);
        pipeline.add_node(drm_node);

        pipeline.register_closer([&pull_to_dec]() { pull_to_dec.close(); });
        pipeline.register_closer([&dec_to_drm]()  { dec_to_drm.close(); });

        /* Install signal handler */
        g_pipeline = &pipeline;
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        /* Init in dependency order */
        pull_node->init();

        /* Pass codec params from puller to decoder */
        dec_node->set_codec_params(pull_node->codec_params());
        dec_node->init();

        drm_node->init();

        /* Start all node threads */
        pipeline.start();

        /* Wait for completion */
        pipeline.wait();

        printf("Done.\n");

    } catch (const std::exception &e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    return 0;
}
