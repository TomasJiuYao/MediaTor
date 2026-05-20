/*
 * V4L2 capture -> RKMPP H264/H265 encode -> file + RTSP stream
 *
 * Capture NV12 1920x1080 from V4L2 device via MMAP,
 * feed into h264_rkmpp/h265_rkmpp encoder, write to file and/or push RTSP.
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
#include <stdexcept>

#include "v4l2_capture.h"
#include "ffmpeg_encoder.h"
#include "rtsp_streamer.h"

int main(int argc, char **argv)
{
    std::cout << "V4L2 -> RKMPP H264/H265 Encoder + RTSP\n"
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
        /* 1. V4L2 capture */
        V4L2Capture cap;
        V4L2Capture::Config cap_cfg;
        cap_cfg.device = device;
        cap.open(cap_cfg);
        cap.start_streaming();

        /* 2. Encoder */
        FFmpegEncoder enc;
        FFmpegEncoder::Config enc_cfg;
        enc_cfg.width   = cap.width();
        enc_cfg.height  = cap.height();
        enc_cfg.codec   = codec_type;
        enc.open(enc_cfg);

        /* 3. Output file */
        FILE *fp = fopen(outfile, "wb");
        if (!fp)
            throw std::runtime_error(std::string("Cannot open ") + outfile + ": " + strerror(errno));
        printf("Writing to %s\n", outfile);

        /* 4. RTSP streamer (optional) */
        RTSPStreamer rtsp;
        if (rtsp_url) {
            RTSPStreamer::Config rtsp_cfg;
            rtsp_cfg.url    = rtsp_url;
            rtsp_cfg.width  = cap.width();
            rtsp_cfg.height = cap.height();
            rtsp.open(rtsp_cfg, enc.codec_ctx());
        }

        /* 5. Packet callback: write to file + push RTSP */
        enc.set_packet_callback([&](const AVPacket *pkt) {
            fwrite(pkt->data, 1, pkt->size, fp);
            if (rtsp_url)
                rtsp.write_packet(pkt);
        });

        /* 6. Capture + Encode loop */
        int frame_cnt = 0;
        int64_t pts   = 0;

        printf("Capturing %d frames...\n", max_frames);

        while (frame_cnt < max_frames) {
            struct v4l2_plane planes[1];
            struct v4l2_buffer vbuf;

            if (!cap.dequeue(vbuf, planes)) {
                usleep(1000);
                continue;
            }

            enc.encode_nv12(cap.buffer(vbuf.index).start, pts++);

            cap.enqueue(vbuf);

            frame_cnt++;
            if (frame_cnt % 30 == 0)
                printf("  encoded %d frames\n", frame_cnt);
        }

        /* 7. Flush */
        enc.flush();

        /* 8. Cleanup */
        fclose(fp);
        cap.stop_streaming();
        /* RTSPStreamer / FFmpegEncoder destructors handle their own cleanup */

        printf("Done. %d frames encoded to %s\n", frame_cnt, outfile);
    } catch (const std::exception &e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    return 0;
}
