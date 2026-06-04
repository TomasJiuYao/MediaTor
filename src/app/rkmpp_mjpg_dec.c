/*
 * RKMPP MJPEG decode demo
 *
 * Read MJPG file (e.g. AVI container), decode with mjpeg_rkmpp hardware decoder,
 * save decoded NV12 frames to a single .yuv file.
 *
 * Build (adjust paths as needed):
 *   gcc -o rkmpp_mjpg_dec rkmpp_mjpg_dec.c \
 *       -I/path/to/ffmpeg-rockchip/rk_out/include \
 *       -L/path/to/ffmpeg-rockchip/rk_out/lib \
 *       -lavcodec -lavformat -lavutil \
 *       -Wl,-rpath,/path/to/ffmpeg-rockchip/rk_out/lib
 *
 * Usage:
 *   ./rkmpp_mjpg_dec input.mjpg [output.yuv]
 *   Defaults: output.yuv
 *
 * Verify output:
 *   ffplay -f rawvideo -pixel_format nv12 -video_size WxH output.yuv
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>

static int write_nv12_frame(AVFrame *frame, FILE *out_fp)
{
    int width  = frame->width;
    int height = frame->height;
    int written = 0;

    for (int i = 0; i < height; i++)
        written += fwrite(frame->data[0] + i * frame->linesize[0], 1, width, out_fp);
    for (int i = 0; i < height / 2; i++)
        written += fwrite(frame->data[1] + i * frame->linesize[1], 1, width, out_fp);

    return written;
}

static int decode_frame(AVCodecContext *dec_ctx, AVFrame *frame,
                        AVPacket *pkt, FILE *out_fp, int *frame_cnt)
{
    int ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        fprintf(stderr, "avcodec_send_packet failed: %s\n", av_err2str(ret));
        return ret;
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return ret;
        if (ret < 0) {
            fprintf(stderr, "avcodec_receive_frame failed: %s\n", av_err2str(ret));
            return ret;
        }

        write_nv12_frame(frame, out_fp);

        (*frame_cnt)++;
        if (*frame_cnt % 30 == 0)
            printf("  decoded %d frames (%dx%d, %s)\n",
                   *frame_cnt, frame->width, frame->height,
                   av_get_pix_fmt_name(frame->format));

        av_frame_unref(frame);
    }

    return 0;
}

static enum AVPixelFormat get_nv12_format(AVCodecContext *ctx,
                                          const enum AVPixelFormat *fmts)
{
    for (const enum AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == AV_PIX_FMT_NV12)
            return AV_PIX_FMT_NV12;
    }
    return AV_PIX_FMT_NONE;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.mjpg> [output.yuv]\n", argv[0]);
        return 1;
    }

    const char *infile  = argv[1];
    const char *outfile = (argc > 2) ? argv[2] : "output.yuv";
    int ret, frame_cnt = 0;
    AVFrame *frame = NULL;
    AVPacket *pkt  = NULL;
    FILE *out_fp   = NULL;

    /* 1. Open input file */
    AVFormatContext *fmt_ctx = NULL;
    ret = avformat_open_input(&fmt_ctx, infile, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Cannot open input '%s': %s\n", infile, av_err2str(ret));
        return 1;
    }

    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Cannot find stream info: %s\n", av_err2str(ret));
        goto fail;
    }

    /* 2. Find video stream */
    int video_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_idx < 0) {
        fprintf(stderr, "No video stream found\n");
        goto fail;
    }

    AVStream *video_st = fmt_ctx->streams[video_idx];
    printf("Video stream #%d: %dx%d, codec_id=%d\n",
           video_idx, video_st->codecpar->width, video_st->codecpar->height,
           video_st->codecpar->codec_id);

    /* 3. Open mjpeg_rkmpp decoder */
    const AVCodec *codec = avcodec_find_decoder_by_name("mjpeg_rkmpp");
    if (!codec) {
        fprintf(stderr, "mjpeg_rkmpp decoder not found\n");
        goto fail;
    }
    printf("Using decoder: %s\n", codec->name);

    AVCodecContext *dec_ctx = avcodec_alloc_context3(codec);
    if (!dec_ctx) {
        fprintf(stderr, "Cannot allocate decoder context\n");
        goto fail;
    }

    ret = avcodec_parameters_to_context(dec_ctx, video_st->codecpar);
    if (ret < 0) {
        fprintf(stderr, "Cannot copy codec params: %s\n", av_err2str(ret));
        goto fail;
    }

    /* Let the decoder negotiate output format via get_format callback.
     * The decoder maps input pix_fmt (e.g. YUVJ420P) to NV12 internally,
     * then does DRM_PRIME -> NV12 transfer in rkmppdec.c. */
    dec_ctx->get_format = get_nv12_format;
    dec_ctx->framerate = (AVRational){ 30, 1 };

    ret = avcodec_open2(dec_ctx, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Cannot open decoder: %s\n", av_err2str(ret));
        goto fail;
    }

    printf("Decoder opened: pix_fmt=%s, sw_pix_fmt=%s\n",
           av_get_pix_fmt_name(dec_ctx->pix_fmt),
           av_get_pix_fmt_name(dec_ctx->sw_pix_fmt));

    /* 4. Open output file */
    out_fp = fopen(outfile, "wb");
    if (!out_fp) {
        fprintf(stderr, "Cannot open output '%s'\n", outfile);
        goto fail;
    }

    /* 5. Decode loop */
    frame = av_frame_alloc();
    pkt   = av_packet_alloc();
    if (!frame || !pkt) {
        fprintf(stderr, "Cannot allocate frame/packet\n");
        goto fail;
    }

    printf("Decoding to %s ...\n", outfile);

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_idx)
            decode_frame(dec_ctx, frame, pkt, out_fp, &frame_cnt);
        av_packet_unref(pkt);
    }

    /* Flush decoder */
    printf("Flushing decoder...\n");
    decode_frame(dec_ctx, frame, NULL, out_fp, &frame_cnt);

    printf("Done. %d frames decoded to %s (%dx%d, nv12)\n",
           frame_cnt, outfile, dec_ctx->width, dec_ctx->height);
    printf("Verify with:\n"
           "  ffplay -f rawvideo -pixel_format nv12 -video_size %dx%d %s\n",
           dec_ctx->width, dec_ctx->height, outfile);

fail:
    if (out_fp)
        fclose(out_fp);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);

    return 0;
}
