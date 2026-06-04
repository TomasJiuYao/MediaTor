/*
 * V4L2 capture -> RKMPP MJPEG encode -> RKMPP MJPEG decode -> NV12 file demo
 *
 * Capture NV12 1920x1080 from /dev/video64 via V4L2 MMAP,
 * feed into mjpeg_rkmpp encoder to get MJPG stream,
 * feed MJPG into mjpeg_rkmpp decoder to get NV12 back,
 * write decoded NV12 frames to output file.
 *
 * Build (adjust paths as needed):
 *   gcc -o v4l2_rkmpp_enc_dec v4l2_rkmpp_enc_dec.c \
 *       -I/path/to/ffmpeg-rockchip/rk_out/include \
 *       -L/path/to/ffmpeg-rockchip/rk_out/lib \
 *       -lavcodec -lavutil \
 *       -Wl,-rpath,/path/to/ffmpeg-rockchip/rk_out/lib
 *
 * Usage:
 *   ./v4l2_rkmpp_enc_dec [output.nv12] [num_frames]
 *   Defaults: output.nv12, 300 frames
 *
 * Verify output:
 *   ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 output.nv12
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>

#define DEVICE       "/dev/video64"
#define WIDTH        1920
#define HEIGHT       1080
#define PIX_FMT_V4L2 V4L2_PIX_FMT_NV12
#define NUM_BUFS     4

/* ---- V4L2 helpers ---- */

struct v4l2_buf {
    void  *start;
    size_t length;
};

static int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do { r = ioctl(fd, request, arg); } while (r == -1 && errno == EINTR);
    return r;
}

static void v4l2_open(int *fd)
{
    *fd = open(DEVICE, O_RDWR | O_NONBLOCK);
    if (*fd < 0) {
        fprintf(stderr, "Cannot open %s: %s\n", DEVICE, strerror(errno));
        exit(1);
    }
}

static void v4l2_init(int fd, int *w, int *h)
{
    struct v4l2_capability cap = {};
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        fprintf(stderr, "VIDIOC_QUERYCAP: %s\n", strerror(errno));
        exit(1);
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "Not a video capture device\n");
        exit(1);
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "Device does not support streaming\n");
        exit(1);
    }

    struct v4l2_format fmt = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .fmt.pix.width       = WIDTH,
        .fmt.pix.height      = HEIGHT,
        .fmt.pix.pixelformat = PIX_FMT_V4L2,
        .fmt.pix.field       = V4L2_FIELD_NONE,
    };
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        fprintf(stderr, "VIDIOC_S_FMT: %s\n", strerror(errno));
        exit(1);
    }
    *w = fmt.fmt.pix.width;
    *h = fmt.fmt.pix.height;
    if (fmt.fmt.pix.pixelformat != PIX_FMT_V4L2) {
        fprintf(stderr, "Device did not accept NV12 format\n");
        exit(1);
    }
    printf("V4L2: %dx%d NV12\n", *w, *h);

    struct v4l2_streamparm parm = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    };
    parm.parm.capture.timeperframe.numerator   = 1;
    parm.parm.capture.timeperframe.denominator = 30;
    xioctl(fd, VIDIOC_S_PARM, &parm);
}

static void v4l2_mmap(int fd, struct v4l2_buf *bufs, int *n_bufs)
{
    struct v4l2_requestbuffers req = {
        .count  = NUM_BUFS,
        .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        fprintf(stderr, "VIDIOC_REQBUFS: %s\n", strerror(errno));
        exit(1);
    }
    if (req.count < 2) {
        fprintf(stderr, "Insufficient buffer memory\n");
        exit(1);
    }
    *n_bufs = req.count;

    for (unsigned i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {
            .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
            .index  = i,
        };
        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            fprintf(stderr, "VIDIOC_QUERYBUF: %s\n", strerror(errno));
            exit(1);
        }
        bufs[i].length = buf.length;
        bufs[i].start  = mmap(NULL, buf.length,
                              PROT_READ | PROT_WRITE, MAP_SHARED,
                              fd, buf.m.offset);
        if (bufs[i].start == MAP_FAILED) {
            fprintf(stderr, "mmap: %s\n", strerror(errno));
            exit(1);
        }
    }
}

static void v4l2_start(int fd)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (unsigned i = 0; i < NUM_BUFS; i++) {
        struct v4l2_buffer buf = {
            .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
            .index  = i,
        };
        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            fprintf(stderr, "VIDIOC_QBUF: %s\n", strerror(errno));
            exit(1);
        }
    }
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        fprintf(stderr, "VIDIOC_STREAMON: %s\n", strerror(errno));
        exit(1);
    }
    printf("V4L2: streaming started\n");
}

static void v4l2_stop(int fd)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(fd, VIDIOC_STREAMOFF, &type);
}

static void v4l2_cleanup(int fd, struct v4l2_buf *bufs, int n_bufs)
{
    for (int i = 0; i < n_bufs; i++)
        munmap(bufs[i].start, bufs[i].length);
    close(fd);
}

/* ---- Write decoded NV12 frame to file ---- */

static int write_nv12_frame(AVFrame *frame, FILE *outfile)
{
    int y_size  = frame->width * frame->height;
    int uv_size = y_size / 2;
    int written = 0;

    written += fwrite(frame->data[0], 1, y_size, outfile);
    written += fwrite(frame->data[1], 1, uv_size, outfile);

    return written;
}

/* ---- Fill NV12 frame from V4L2 buffer ---- */

static void fill_nv12_frame(AVFrame *frame, const void *data, int width, int height)
{
    int y_size = width * height;

    memcpy(frame->data[0], data, y_size);
    memcpy(frame->data[1], (const uint8_t *)data + y_size, y_size / 2);
}

/* ---- Main ---- */

int main(int argc, char **argv)
{
    const char *outfile = (argc > 1) ? argv[1] : "output.nv12";
    int max_frames      = (argc > 2) ? atoi(argv[2]) : 300;

    int vfd, n_bufs, cap_w, cap_h;
    struct v4l2_buf bufs[NUM_BUFS];

    /* 1. V4L2 init */
    v4l2_open(&vfd);
    v4l2_init(vfd, &cap_w, &cap_h);
    v4l2_mmap(vfd, bufs, &n_bufs);
    v4l2_start(vfd);

    /* 2. MJPEG encoder init — mjpeg_rkmpp */
    const AVCodec *enc_codec = avcodec_find_encoder_by_name("mjpeg_rkmpp");
    if (!enc_codec) {
        fprintf(stderr, "mjpeg_rkmpp encoder not found\n");
        exit(1);
    }
    printf("Encoder: %s\n", enc_codec->name);

    AVCodecContext *enc_ctx = avcodec_alloc_context3(enc_codec);
    if (!enc_ctx) {
        fprintf(stderr, "Could not allocate encoder context\n");
        exit(1);
    }

    enc_ctx->bit_rate  = 8 * 1000 * 1000;   /* 8 Mbps */
    enc_ctx->width     = cap_w;
    enc_ctx->height    = cap_h;
    enc_ctx->time_base = (AVRational){ 1, 30 };
    enc_ctx->framerate = (AVRational){ 30, 1 };
    enc_ctx->gop_size  = 1;                  /* MJPEG: every frame is keyframe */
    enc_ctx->pix_fmt   = AV_PIX_FMT_NV12;

    av_opt_set(enc_ctx->priv_data, "rc_mode", "CBR", 0);

    int ret = avcodec_open2(enc_ctx, enc_codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open encoder: %s\n", av_err2str(ret));
        exit(1);
    }

    /* 3. MJPEG decoder init — mjpeg_rkmpp */
    const AVCodec *dec_codec = avcodec_find_decoder_by_name("mjpeg_rkmpp");
    if (!dec_codec) {
        fprintf(stderr, "mjpeg_rkmpp decoder not found\n");
        exit(1);
    }
    printf("Decoder: %s\n", dec_codec->name);

    AVCodecContext *dec_ctx = avcodec_alloc_context3(dec_codec);
    if (!dec_ctx) {
        fprintf(stderr, "Could not allocate decoder context\n");
        exit(1);
    }

    dec_ctx->pix_fmt   = AV_PIX_FMT_NV12;
    dec_ctx->framerate = (AVRational){ 30, 1 };

    ret = avcodec_open2(dec_ctx, dec_codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open decoder: %s\n", av_err2str(ret));
        exit(1);
    }

    /* 4. Prepare AVFrame for encoder input */
    AVFrame *enc_frame = av_frame_alloc();
    enc_frame->format = AV_PIX_FMT_NV12;
    enc_frame->width  = cap_w;
    enc_frame->height = cap_h;
    ret = av_frame_get_buffer(enc_frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate frame: %s\n", av_err2str(ret));
        exit(1);
    }

    AVPacket *pkt = av_packet_alloc();
    AVFrame  *dec_frame = av_frame_alloc();

    /* 5. Open output file */
    FILE *fp = fopen(outfile, "wb");
    if (!fp) {
        fprintf(stderr, "Cannot open %s: %s\n", outfile, strerror(errno));
        exit(1);
    }
    printf("Writing decoded NV12 to %s\n", outfile);
    printf("NV12 frame size: %d bytes (%dx%d)\n",
           cap_w * cap_h * 3 / 2, cap_w, cap_h);

    /* 6. Capture -> Encode -> Decode -> Write loop */
    int frame_cnt  = 0;
    int dec_cnt    = 0;
    int64_t pts    = 0;

    printf("Capturing %d frames...\n", max_frames);

    while (frame_cnt < max_frames) {
        /* Dequeue a filled V4L2 buffer */
        struct v4l2_buffer vbuf = {
            .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
        };
        if (xioctl(vfd, VIDIOC_DQBUF, &vbuf) < 0) {
            if (errno == EAGAIN) {
                usleep(1000);
                continue;
            }
            fprintf(stderr, "VIDIOC_DQBUF: %s\n", strerror(errno));
            break;
        }

        /* Fill AVFrame from V4L2 buffer */
        av_frame_make_writable(enc_frame);
        fill_nv12_frame(enc_frame, bufs[vbuf.index].start, cap_w, cap_h);
        enc_frame->pts = pts++;

        /* --- Encode NV12 -> MJPG --- */
        ret = avcodec_send_frame(enc_ctx, enc_frame);
        if (ret < 0) {
            fprintf(stderr, "encode send_frame: %s\n", av_err2str(ret));
            goto requeue;
        }

        while (ret >= 0) {
            ret = avcodec_receive_packet(enc_ctx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            if (ret < 0) {
                fprintf(stderr, "encode receive_packet: %s\n", av_err2str(ret));
                break;
            }

            /* --- Decode MJPG -> NV12 --- */
            int dret = avcodec_send_packet(dec_ctx, pkt);
            if (dret < 0) {
                fprintf(stderr, "decode send_packet: %s\n", av_err2str(dret));
                av_packet_unref(pkt);
                continue;
            }

            while (dret >= 0) {
                dret = avcodec_receive_frame(dec_ctx, dec_frame);
                if (dret == AVERROR(EAGAIN) || dret == AVERROR_EOF)
                    break;
                if (dret < 0) {
                    fprintf(stderr, "decode receive_frame: %s\n", av_err2str(dret));
                    break;
                }

                /* Write decoded NV12 to file */
                write_nv12_frame(dec_frame, fp);
                dec_cnt++;
                av_frame_unref(dec_frame);
            }

            av_packet_unref(pkt);
        }

requeue:
        /* Re-queue the V4L2 buffer */
        if (xioctl(vfd, VIDIOC_QBUF, &vbuf) < 0) {
            fprintf(stderr, "VIDIOC_QBUF: %s\n", strerror(errno));
            break;
        }

        frame_cnt++;
        if (frame_cnt % 30 == 0)
            printf("  captured %d frames, decoded %d frames\n", frame_cnt, dec_cnt);
    }

    /* 7. Flush encoder -> decoder pipeline */
    printf("Flushing pipeline...\n");

    /* Flush encoder */
    avcodec_send_frame(enc_ctx, NULL);
    while (1) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0) {
            fprintf(stderr, "flush encode: %s\n", av_err2str(ret));
            break;
        }

        /* Send remaining MJPG packets to decoder */
        avcodec_send_packet(dec_ctx, pkt);
        av_packet_unref(pkt);
    }

    /* Flush decoder */
    avcodec_send_packet(dec_ctx, NULL);
    while (1) {
        ret = avcodec_receive_frame(dec_ctx, dec_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0) {
            fprintf(stderr, "flush decode: %s\n", av_err2str(ret));
            break;
        }
        write_nv12_frame(dec_frame, fp);
        dec_cnt++;
        av_frame_unref(dec_frame);
    }

    /* 8. Cleanup */
    fclose(fp);
    v4l2_stop(vfd);
    v4l2_cleanup(vfd, bufs, n_bufs);

    av_frame_free(&enc_frame);
    av_frame_free(&dec_frame);
    av_packet_free(&pkt);
    avcodec_free_context(&enc_ctx);
    avcodec_free_context(&dec_ctx);

    printf("Done. Captured %d frames, decoded %d NV12 frames written to %s\n",
           frame_cnt, dec_cnt, outfile);
    return 0;
}
