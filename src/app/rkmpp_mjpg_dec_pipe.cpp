/*
 * MJPEG file -> RKMPP decode -> NV12 file save (pipeline)
 *
 * Pipeline:
 *   MjpegFileReaderNode --[Packet]--> RKMPPDecoderNode(MJPEG) --[DecodedFrame]--> NV12FileWriterNode
 *
 * Usage:
 *   ./rkmpp_mjpg_dec_pipe [input.mjpg] [output.nv12]
 *   Defaults: output.mjpg, output.nv12
 *
 * Verify output:
 *   ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 output.nv12
 */

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <signal.h>

#include "mjpeg_file_reader_node.h"
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
    std::cout << "MJPEG File -> RKMPP Decode -> NV12 Save\n"
              << "\n"
              << "Usage: rkmpp_mjpg_dec_pipe [input.mjpg] [output.nv12]\n"
              << std::endl;

    const char *infile  = (argc > 1) ? argv[1] : "output.mjpg";
    const char *outfile = (argc > 2) ? argv[2] : "output.nv12";

    try {
        /* Create queues */
        BlockingQueue<Packet>        reader_to_dec(8);
        BlockingQueue<DecodedFrame>  dec_to_file(4);

        /* Create reader node first to get stream info */
        auto reader = std::make_shared<MjpegFileReaderNode>(infile);
        reader->init();

        /* Configure decoder with stream info */
        RKMPPDecoderNode::Config dec_cfg;
        dec_cfg.codec  = RKMPPDecoderNode::MJPEG;
        dec_cfg.width  = reader->width();
        dec_cfg.height = reader->height();

        auto decoder = std::make_shared<RKMPPDecoderNode>(dec_cfg);
        decoder->set_codec_params(reader->codec_params());

        auto writer = std::make_shared<NV12FileWriterNode>(outfile);

        /* Wire up queues */
        reader->add_output(&reader_to_dec);

        decoder->set_input(&reader_to_dec);
        decoder->add_output(&dec_to_file);

        writer->set_input(&dec_to_file);

        /* Init remaining nodes */
        decoder->init();
        writer->init();

        /* Build pipeline */
        Pipeline pipeline;
        pipeline.add_node(reader);
        pipeline.add_node(decoder);
        pipeline.add_node(writer);

        pipeline.register_closer([&reader_to_dec]() { reader_to_dec.close(); });
        pipeline.register_closer([&dec_to_file]()   { dec_to_file.close(); });

        /* Install signal handler */
        g_pipeline = &pipeline;
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

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
