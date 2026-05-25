/*
 * RTSP pull -> RKMPP decode -> NV12 file save
 *
 * Usage:
 *   ./rtsp_pull_decode_nv12 [rtsp_url] [output.yuv] [max_frames] [codec]
 *   Defaults: rtsp://192.168.42.110:8554/live, output.nv12, 100, h264
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <stdexcept>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/imgutils.h>
}

static volatile int g_running = 1;
static void signal_handler(int) { g_running = 0; }

static char err_buf[AV_ERROR_MAX_STRING_SIZE] = {};
static const char *av_err(int e) {
    return av_make_error_string(err_buf, AV_ERROR_MAX_STRING_SIZE, e);
}

int main(int argc, char **argv)
{
    printf("RTSP Pull -> RKMPP Decode -> NV12 Save\n\n"
           "Usage: rtsp_pull_decode_nv12 [rtsp_url] [output] [max_frames] [codec]\n\n");

    const char *url       = (argc > 1) ? argv[1] : "rtsp://192.168.42.110:8554/live";
    const char *outfile   = (argc > 2) ? argv[2] : "output.nv12";
    int max_frames        = (argc > 3) ? atoi(argv[3]) : 100;
    const char *codec_str = (argc > 4) ? argv[4] : "h264";

    printf("URL: %s\nOutput: %s\nFrames: %d\nCodec: %s\n\n",
           url, outfile, max_frames, codec_str);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* --- Open RTSP --- */
    AVFormatContext *fmt_ctx = nullptr;
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "stimeout", "5000000", 0);
    av_dict_set(&opts, "fflags", "nobuffer", 0);
    av_dict_set(&opts, "allowed_media_types", "video", 0);

    int ret = avformat_open_input(&fmt_ctx, url, nullptr, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        fprintf(stderr, "Could not open RTSP: %s\n", av_err(ret));
        return 1;
    }

    ret = avformat_find_stream_info(fmt_ctx, nullptr);
    if (ret < 0) {
        fprintf(stderr, "Could not find stream info: %s\n", av_err(ret));
        return 1;
    }

    int video_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_idx < 0) {
        fprintf(stderr, "No video stream found\n");
        return 1;
    }

    printf("Video stream %d: %dx%d\n", video_idx,
           fmt_ctx->streams[video_idx]->codecpar->width,
           fmt_ctx->streams[video_idx]->codecpar->height);

    /* --- Open decoder --- */
    const char *dec_name = (strcmp(codec_str, "h265") == 0 || strcmp(codec_str, "hevc") == 0)
                            ? "hevc_rkmpp" : "h264_rkmpp";
    const AVCodec *codec = avcodec_find_decoder_by_name(dec_name);
    if (!codec) {
        fprintf(stderr, "%s not found, trying software decoder\n", dec_name);
        dec_name = (strcmp(codec_str, "h265") == 0 || strcmp(codec_str, "hevc") == 0)
                    ? "hevc" : "h264";
        codec = avcodec_find_decoder_by_name(dec_name);
    }
    if (!codec) {
        fprintf(stderr, "No decoder found\n");
        return 1;
    }
    printf("Decoder: %s\n", codec->name);

    AVCodecContext *dec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[video_idx]->codecpar);

    bool is_rkmpp = (strstr(codec->name, "rkmpp") != nullptr);
    AVBufferRef *hw_dev_ctx = nullptr;

    if (is_rkmpp) {
        ret = av_hwdevice_ctx_create(&hw_dev_ctx, AV_HWDEVICE_TYPE_DRM,
                                      "/dev/card0", nullptr, 0);
        if (ret < 0) {
            fprintf(stderr, "Could not create DRM hw device: %s\n", av_err(ret));
        } else {
            dec_ctx->hw_device_ctx = av_buffer_ref(hw_dev_ctx);
            printf("DRM hw device created\n");
        }
    }

    dec_ctx->thread_count = 1;
    ret = avcodec_open2(dec_ctx, codec, nullptr);
    if (ret < 0) {
        fprintf(stderr, "Could not open decoder: %s\n", av_err(ret));
        return 1;
    }

    /* --- Open output file --- */
    FILE *fp = fopen(outfile, "wb");
    if (!fp) {
        fprintf(stderr, "Could not open output file: %s\n", outfile);
        return 1;
    }

    /* --- Decode loop --- */
    int frame_count = 0;
    int64_t file_size = 0;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *sw_frame = av_frame_alloc();

    printf("Decoding...\n");

    while (g_running && frame_count < max_frames) {
        ret = av_read_frame(fmt_ctx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF)
                printf("EOF\n");
            else
                fprintf(stderr, "Read error: %s\n", av_err(ret));
            break;
        }

        if (pkt->stream_index != video_idx) {
            av_packet_unref(pkt);
            continue;
        }

        ret = avcodec_send_packet(dec_ctx, pkt);
        av_packet_unref(pkt);
        if (ret < 0) {
            fprintf(stderr, "Send packet error: %s\n", av_err(ret));
            continue;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(dec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            if (ret < 0) {
                fprintf(stderr, "Receive frame error: %s\n", av_err(ret));
                break;
            }

            /* Transfer from GPU (DRM_PRIME) to CPU (NV12) */
            if (frame->format == AV_PIX_FMT_DRM_PRIME) {
                sw_frame->format = AV_PIX_FMT_NV12;
                ret = av_hwframe_transfer_data(sw_frame, frame, 0);
                if (ret < 0) {
                    fprintf(stderr, "hwframe transfer failed: %s\n", av_err(ret));
                    av_frame_unref(frame);
                    continue;
                }
                sw_frame->width  = frame->width;
                sw_frame->height = frame->height;
            } 
            else 
            {
                printf("Frame already in software format: %s\n",
                       av_get_pix_fmt_name((AVPixelFormat)frame->format));
                /* Already software format */
                av_frame_move_ref(sw_frame, frame);
                frame = av_frame_alloc();
            }

            if (frame_count == 0) {
                printf("First frame: %dx%d, format=%s\n",
                       sw_frame->width, sw_frame->height,
                       av_get_pix_fmt_name((AVPixelFormat)sw_frame->format));
            }

            /* Write NV12: Y plane + UV plane */
            int w = sw_frame->width;
            int h = sw_frame->height;

            /* Y */
            for (int i = 0; i < h; i++) {
                fwrite(sw_frame->data[0] + i * sw_frame->linesize[0], 1, w, fp);
            }
            /* UV (NV12: interleaved) */
            for (int i = 0; i < h / 2; i++) {
                fwrite(sw_frame->data[1] + i * sw_frame->linesize[1], 1, w, fp);
            }

            file_size += w * h * 3 / 2; /* NV12 size */
            frame_count++;

            if (frame_count % 30 == 0) {
                printf("  %d frames, %.1f MB\n", frame_count, file_size / 1024.0 / 1024.0);
            }

            av_frame_unref(sw_frame);
            av_frame_unref(frame);
        }
    }

    /* Flush decoder */
    avcodec_send_packet(dec_ctx, nullptr);

    fclose(fp);
    av_frame_free(&sw_frame);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec_ctx);
    if (hw_dev_ctx) av_buffer_unref(&hw_dev_ctx);
    avformat_close_input(&fmt_ctx);

    printf("Done: %d frames saved to %s (%.1f MB)\n",
           frame_count, outfile, file_size / 1024.0 / 1024.0);

    return 0;
}
