/*
 * V4L2 capture -> RKMPP H264/H265 encode -> file + RTSP stream
 *
 * Pipeline architecture with epoll-based V4L2 capture:
 *   V4L2CaptureNode --[Frame]--> FFmpegEncoderNode --[Packet]--> RTSPStreamerNode
 *                                                    |
 *                                                    +--[Packet]--> FileWriterNode
 *
 * Usage:
 *   ./v4l2_rkmpp_enc [device] [output] [num_frames] [h264|h265] [rtsp_url]
 *   Defaults: /dev/video55, output.h264, 300 frames, h264, no RTSP
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
    std::cout << "V4L2 -> RKMPP H264/H265 Encoder + RTSP (Pipeline)\n"
              << "\n"
              << "Usage: v4l2_rkmpp_enc [device] [output] [num_frames] [codec] [rtsp_url]\n"
              << "\n"
              << "Arguments:\n"
              << "  device      V4L2 video device path   (default: /dev/video55)\n"
              << "  output      Output file path          (default: output.h264)\n"
              << "  num_frames  Number of frames to encode (default: 300)\n"
              << "  codec       Encoding codec: h264|h265  (default: h264)\n"
              << "  rtsp_url    RTSP push URL, omit to disable (default: disabled)\n"
              << "\n"
              << "Examples:\n"
              << "  ./v4l2_rkmpp_enc\n"
              << "  ./v4l2_rkmpp_enc /dev/video0 out.h265 500 h265\n"
              << "  ./v4l2_rkmpp_enc /dev/video64 stream.h264 30000 h264 rtsp://192.168.42.110:8554/live\n"
              << std::endl;

    const char *device     = (argc > 1) ? argv[1] : "/dev/video55";
    const char *outfile    = (argc > 2) ? argv[2] : "output.h264";
    int max_frames         = (argc > 3) ? atoi(argv[3]) : 300;
    const char *codec_str  = (argc > 4) ? argv[4] : "h264";
    const char *rtsp_url   = (argc > 5) ? argv[5] : nullptr;

    FFmpegEncoder::Codec codec_type = FFmpegEncoder::H264;
    if (strcmp(codec_str, "h265") == 0 || strcmp(codec_str, "hevc") == 0) {
        codec_type = FFmpegEncoder::H265;
    }

    std::cout << "Device: " << device
              << ", Output: " << outfile
              << ", Frames: " << max_frames
              << ", Codec: " << (codec_type == FFmpegEncoder::H265 ? "H265" : "H264")
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
        enc_cfg.codec = codec_type;

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
