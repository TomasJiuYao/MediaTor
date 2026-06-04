/*
 * V4L2 capture -> RKMPP MJPEG encode demo
 *
 * Capture NV12 1920x1080 from /dev/video64 via V4L2 MMAP,
 * feed into mjpeg_rkmpp encoder, write MJPG elementary stream to file.
 *
 * Build (adjust paths as needed):
 *   gcc -o v4l2_rkmpp_enc v4l2_rkmpp_enc.c \
 *       -I/path/to/ffmpeg-rockchip/rk_out/include \
 *       -L/path/to/ffmpeg-rockchip/rk_out/lib \
 *       -lavcodec -lavutil \
 *       -Wl,-rpath,/path/to/ffmpeg-rockchip/rk_out/lib
 *
 * Usage:
 *   ./v4l2_rkmpp_enc [output.mjpg] [num_frames]
 *   Defaults: output.mjpg, 300 frames
 */

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
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
        //exit(1);
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "Not a video capture device\n");
        //exit(1);
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "Device does not support streaming\n");
        //exit(1);
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
    /* Report what the driver actually gave us */
    *w = fmt.fmt.pix.width;
    *h = fmt.fmt.pix.height;
    if (fmt.fmt.pix.pixelformat != PIX_FMT_V4L2) {
        fprintf(stderr, "Device did not accept NV12 format\n");
        exit(1);
    }
    printf("V4L2: %dx%d NV12\n", *w, *h);

    /* Set frame rate */
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

/* ---- Encode helper ---- */

static void encode(AVCodecContext *enc_ctx, AVFrame *frame,
                   AVPacket *pkt, FILE *outfile)
{
    int ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "avcodec_send_frame: %s\n", av_err2str(ret));
        return;
    }
    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        if (ret < 0) {
            fprintf(stderr, "avcodec_receive_packet: %s\n", av_err2str(ret));
            return;
        }
        fwrite(pkt->data, 1, pkt->size, outfile);
        av_packet_unref(pkt);
    }
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
    const char *outfile = (argc > 1) ? argv[1] : "output.mjpg";
    int max_frames      = (argc > 2) ? atoi(argv[2]) : 300;

    int vfd, n_bufs, cap_w, cap_h;
    struct v4l2_buf bufs[NUM_BUFS];

    /* 1. V4L2 init */
    v4l2_open(&vfd);
    v4l2_init(vfd, &cap_w, &cap_h);
    v4l2_mmap(vfd, bufs, &n_bufs);
    v4l2_start(vfd);

    /* 2. MJPEG encoder init — mjpeg_rkmpp */
    const AVCodec *codec = avcodec_find_encoder_by_name("mjpeg_rkmpp");
    if (!codec) {
        fprintf(stderr, "mjpeg_rkmpp encoder not found\n");
        exit(1);
    }
    printf("Using encoder: %s\n", codec->name);

    AVCodecContext *enc_ctx = avcodec_alloc_context3(codec);
    if (!enc_ctx) {
        fprintf(stderr, "Could not allocate encoder context\n");
        exit(1);
    }

    enc_ctx->bit_rate  = 8 * 1000 * 1000;   /* 8 Mbps for MJPEG */
    enc_ctx->width     = cap_w;
    enc_ctx->height    = cap_h;
    enc_ctx->time_base = (AVRational){ 1, 30 };
    enc_ctx->framerate = (AVRational){ 30, 1 };
    enc_ctx->gop_size  = 1;                  /* MJPEG: every frame is keyframe */
    enc_ctx->pix_fmt   = AV_PIX_FMT_NV12;

    av_opt_set(enc_ctx->priv_data, "rc_mode", "CBR", 0);

    int ret = avcodec_open2(enc_ctx, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open encoder: %s\n", av_err2str(ret));
        exit(1);
    }

    /* 3. Prepare AVFrame for encoder input */
    AVFrame *frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_NV12;
    frame->width  = cap_w;
    frame->height = cap_h;
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate frame: %s\n", av_err2str(ret));
        exit(1);
    }

    AVPacket *pkt = av_packet_alloc();

    /* 4. Open output file */
    FILE *fp = fopen(outfile, "wb");
    if (!fp) {
        fprintf(stderr, "Cannot open %s: %s\n", outfile, strerror(errno));
        exit(1);
    }
    printf("Writing MJPG to %s\n", outfile);

    /* 5. Capture + Encode loop */
    int frame_cnt = 0;
    int64_t pts   = 0;

    printf("Capturing %d frames...\n", max_frames);

    while (frame_cnt < max_frames) {
        /* Dequeue a filled buffer */
        struct v4l2_buffer vbuf = {
            .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
        };
        if (xioctl(vfd, VIDIOC_DQBUF, &vbuf) < 0) {
            if (errno == EAGAIN) {
                usleep(1000);  /* 1 ms, then retry */
                continue;
            }
            fprintf(stderr, "VIDIOC_DQBUF: %s\n", strerror(errno));
            break;
        }

        /* Fill AVFrame from V4L2 buffer and encode */
        av_frame_make_writable(frame);
        fill_nv12_frame(frame, bufs[vbuf.index].start, cap_w, cap_h);
        frame->pts = pts++;
        encode(enc_ctx, frame, pkt, fp);

        /* Re-queue the buffer */
        if (xioctl(vfd, VIDIOC_QBUF, &vbuf) < 0) {
            fprintf(stderr, "VIDIOC_QBUF: %s\n", strerror(errno));
            break;
        }

        frame_cnt++;
        if (frame_cnt % 30 == 0)
            printf("  encoded %d frames\n", frame_cnt);
    }

    /* 6. Flush encoder */
    printf("Flushing encoder...\n");
    encode(enc_ctx, NULL, pkt, fp);

    /* 7. Cleanup */
    fclose(fp);
    v4l2_stop(vfd);
    v4l2_cleanup(vfd, bufs, n_bufs);

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&enc_ctx);

    printf("Done. %d frames encoded to %s\n", frame_cnt, outfile);
    return 0;
}
