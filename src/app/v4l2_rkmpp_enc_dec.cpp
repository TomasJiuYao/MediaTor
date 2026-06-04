/*
 * V4L2 capture -> RKMPP MJPEG encode -> RKMPP MJPEG decode -> NV12 file save
 *
 * Pipeline:
 *   V4L2CaptureNode --[Frame]--> FFmpegEncoderNode(MJPEG) --[Packet]--> FileWriterNode (save .mjpg)
 *                                                   --[Packet]--> RKMPPDecoderNode(MJPEG) --[DecodedFrame]--> NV12FileWriterNode
 *
 * Usage:
 *   ./v4l2_rkmpp_enc_dec [device] [output.nv12] [mjpg_file] [num_frames]
 *   Defaults: /dev/video64, output.nv12, output.mjpg, 300
 *
 * Verify output:
 *   ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 output.nv12
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
#include "file_writer_node.h"
#include "rkmpp_decoder.h"
#include "nv12_file_writer_node.h"
#include "pipeline.h"
#include "blocking_queue.h"
#include "common.h"

static Pipeline *g_pipeline = nullptr;

static void signal_handler(int) {
    if (g_pipeline) g_pipeline->stop();
}

int main(int argc, char **argv)
{
    std::cout << "V4L2 -> MJPEG Encode -> MJPEG Decode -> NV12 Save\n"
              << "\n"
              << "Usage: v4l2_rkmpp_enc_dec [device] [output.nv12] [mjpg_file] [num_frames]\n"
              << "\n"
              << "Arguments:\n"
              << "  device      V4L2 video device path   (default: /dev/video64)\n"
              << "  output      Output NV12 file path     (default: output.nv12)\n"
              << "  mjpg_file   Output MJPG file path     (default: output.mjpg)\n"
              << "  num_frames  Number of frames           (default: 300)\n"
              << std::endl;

    const char *device     = (argc > 1) ? argv[1] : "/dev/video64";
    const char *outfile    = (argc > 2) ? argv[2] : "output.nv12";
    const char *mjpgfile   = (argc > 3) ? argv[3] : "output.mjpg";
    int max_frames         = (argc > 4) ? atoi(argv[4]) : 300;

    std::cout << "Device: " << device
              << ", Output: " << outfile
              << ", MJPG: " << mjpgfile
              << ", Frames: " << max_frames
              << std::endl;

    try {
        /* Create queues */
        BlockingQueue<Frame>         cap_to_enc(4);   /* capture -> encoder */
        BlockingQueue<Packet>        enc_to_mjpg(8);  /* encoder -> mjpg file */
        BlockingQueue<Packet>        enc_to_dec(8);   /* encoder -> decoder */
        BlockingQueue<DecodedFrame>  dec_to_file(4);  /* decoder -> file writer */

        /* Create nodes (decoder deferred until encoder init provides codec info) */
        V4L2CaptureNode::Config cap_cfg;
        cap_cfg.device = device;

        FFmpegEncoderNode::Config enc_cfg;
        enc_cfg.codec   = FFmpegEncoder::MJPEG;
        enc_cfg.bitrate = 8 * 1000 * 1000;  /* 8 Mbps for MJPEG */

        auto cap_node   = std::make_shared<V4L2CaptureNode>(cap_cfg);
        auto enc_node   = std::make_shared<FFmpegEncoderNode>(enc_cfg);
        auto mjpg_node  = std::make_shared<FileWriterNode>(mjpgfile);
        auto file_node  = std::make_shared<NV12FileWriterNode>(outfile, max_frames);

        /* Wire up capture -> encoder -> mjpg file writer */
        cap_node->set_output(&cap_to_enc);

        enc_node->set_input(&cap_to_enc);
        enc_node->add_output(&enc_to_mjpg);
        enc_node->add_output(&enc_to_dec);

        mjpg_node->set_input(&enc_to_mjpg);

        /* Build pipeline (add decoder later) */
        Pipeline pipeline;
        pipeline.add_node(cap_node);
        pipeline.add_node(enc_node);
        pipeline.add_node(mjpg_node);

        pipeline.register_closer([&cap_to_enc]()   { cap_to_enc.close(); });
        pipeline.register_closer([&enc_to_mjpg]()   { enc_to_mjpg.close(); });
        pipeline.register_closer([&enc_to_dec]()    { enc_to_dec.close(); });
        pipeline.register_closer([&dec_to_file]()   { dec_to_file.close(); });

        /* Install signal handler for graceful shutdown */
        g_pipeline = &pipeline;
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        /* Init capture and encoder first to get resolution/codec info */
        cap_node->init();

        enc_node->set_resolution(cap_node->width(), cap_node->height());
        enc_node->init();

        /* Now create decoder — MJPEG decoder only needs codec type and resolution,
         * not encoder's codec_params (which carries NV12 pix_fmt that mjpeg_rkmpp rejects) */
        RKMPPDecoderNode::Config dec_cfg;
        dec_cfg.codec  = RKMPPDecoderNode::MJPEG;
        dec_cfg.width  = cap_node->width();
        dec_cfg.height = cap_node->height();

        auto dec_node = std::make_shared<RKMPPDecoderNode>(dec_cfg);

        /* Wire up decoder -> nv12 file writer */
        dec_node->set_input(&enc_to_dec);
        dec_node->add_output(&dec_to_file);

        file_node->set_input(&dec_to_file);

        pipeline.add_node(dec_node);
        pipeline.add_node(file_node);

        /* Init remaining nodes */
        dec_node->init();

        mjpg_node->init();
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
