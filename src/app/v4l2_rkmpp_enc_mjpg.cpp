/*
 * V4L2 capture -> RKMPP MJPEG encode -> file (Pipeline)
 *
 * Pipeline architecture:
 *   V4L2CaptureNode --[Frame]--> FFmpegEncoderNode --[Packet]--> FileWriterNode
 *
 * Usage:
 *   ./v4l2_rkmpp_enc_mjpg [device] [output] [num_frames] [rtsp_url]
 *   Defaults: /dev/video64, output.mjpg, 300 frames, no RTSP
 */

#define _DEFAULT_SOURCE
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <signal.h>

#include "v4l2_capture_node.h"
#include "ffmpeg_encoder_node.h"
#include "rtsp_streamer_node.h"
#include "file_writer_node.h"
#include "pipeline.h"
#include "blocking_queue.h"
#include "common.h"

static Pipeline *g_pipeline = nullptr;

static void signal_handler(int) {
    if (g_pipeline) g_pipeline->stop();
}

int main(int argc, char **argv)
{
    std::cout << "V4L2 -> RKMPP MJPEG Encoder (Pipeline)\n"
              << "\n"
              << "Usage: v4l2_rkmpp_enc_mjpg [device] [output] [num_frames] [rtsp_url]\n"
              << "\n"
              << "Arguments:\n"
              << "  device      V4L2 video device path   (default: /dev/video64)\n"
              << "  output      Output file path          (default: output.mjpg)\n"
              << "  num_frames  Number of frames to encode (default: 300)\n"
              << "  rtsp_url    RTSP push URL, omit to disable (default: disabled)\n"
              << "\n"
              << "Examples:\n"
              << "  ./v4l2_rkmpp_enc_mjpg\n"
              << "  ./v4l2_rkmpp_enc_mjpg /dev/video0 out.mjpg 500\n"
              << "  ./v4l2_rkmpp_enc_mjpg /dev/video64 stream.mjpg 30000 rtsp://192.168.42.110:8554/live\n"
              << std::endl;

    const char *device     = (argc > 1) ? argv[1] : "/dev/video64";
    const char *outfile    = (argc > 2) ? argv[2] : "output.mjpg";
    int max_frames         = (argc > 3) ? atoi(argv[3]) : 300;
    const char *rtsp_url   = (argc > 4) ? argv[4] : nullptr;

    std::cout << "Device: " << device
              << ", Output: " << outfile
              << ", Frames: " << max_frames
              << ", Codec: MJPEG"
              << ", RTSP: " << (rtsp_url ? rtsp_url : "disabled")
              << std::endl;

    try {
        /* Create queues */
        BlockingQueue<Frame>  cap_to_enc(4);   /* capture -> encoder */
        BlockingQueue<Packet> enc_to_file(8);   /* encoder -> file writer */
        BlockingQueue<Packet> enc_to_rtsp(8);   /* encoder -> rtsp (optional) */

        /* Create nodes */
        V4L2CaptureNode::Config cap_cfg;
        cap_cfg.device = device;

        FFmpegEncoderNode::Config enc_cfg;
        enc_cfg.codec   = FFmpegEncoder::MJPEG;
        enc_cfg.bitrate = 8 * 1000 * 1000;  /* 8 Mbps for MJPEG */

        auto cap_node  = std::make_shared<V4L2CaptureNode>(cap_cfg);
        auto enc_node  = std::make_shared<FFmpegEncoderNode>(enc_cfg);
        auto file_node = std::make_shared<FileWriterNode>(outfile);

        /* Wire up queues */
        cap_node->set_output(&cap_to_enc);
        enc_node->set_input(&cap_to_enc);
        enc_node->add_output(&enc_to_file);
        file_node->set_input(&enc_to_file);

        /* Build pipeline */
        Pipeline pipeline;
        pipeline.add_node(cap_node);
        pipeline.add_node(enc_node);
        pipeline.add_node(file_node);

        pipeline.register_closer([&cap_to_enc]() { cap_to_enc.close(); });
        pipeline.register_closer([&enc_to_file]() { enc_to_file.close(); });

        /* Optional RTSP */
        std::shared_ptr<RTSPStreamerNode> rtsp_node;
        if (rtsp_url) {
            RTSPStreamerNode::Config rtsp_cfg;
            rtsp_cfg.url = rtsp_url;

            rtsp_node = std::make_shared<RTSPStreamerNode>(rtsp_cfg);
            rtsp_node->set_input(&enc_to_rtsp);
            enc_node->add_output(&enc_to_rtsp);
            pipeline.add_node(rtsp_node);
            pipeline.register_closer([&enc_to_rtsp]() { enc_to_rtsp.close(); });
        }

        /* Install signal handler for graceful shutdown */
        g_pipeline = &pipeline;
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        /* Init nodes in dependency order */
        cap_node->init();
        enc_node->set_resolution(cap_node->width(), cap_node->height());
        enc_node->init();

        if (rtsp_node) {
            rtsp_node->set_codec_ctx(enc_node->codec_ctx());
        }

        file_node->init();

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
