#pragma once

#include <cstddef>
#include <cstdint>
#include <linux/videodev2.h>

class V4L2Capture {
public:
    struct Buffer {
        void  *start;
        size_t length;
    };

    struct Config {
        const char *device    = "/dev/video55";
        int         width     = 1920;
        int         height    = 1080;
        uint32_t    pixfmt    = V4L2_PIX_FMT_NV12;
        int         num_bufs  = 4;
        int         fps       = 30;
    };

    V4L2Capture();
    ~V4L2Capture();

    V4L2Capture(const V4L2Capture &) = delete;
    V4L2Capture &operator=(const V4L2Capture &) = delete;

    void open(const Config &cfg);
    void start_streaming();
    void stop_streaming();

    /* Dequeue a buffer. Returns false on EAGAIN (no frame ready). */
    bool dequeue(struct v4l2_buffer &vbuf, struct v4l2_plane *planes);

    /* Re-queue a buffer after use */
    void enqueue(struct v4l2_buffer &vbuf);

    /* Re-queue a buffer by index (convenience for Frame release callback) */
    void enqueue_by_index(int index);

    /* Expose fd for epoll */
    int fd() const;

    const Buffer &buffer(int index) const;
    int  width()     const;
    int  height()    const;
    bool mplane()    const;

private:
    static constexpr int FMT_NUM_PLANES = 1;
    static constexpr int MAX_BUFS       = 8;

    int  xioctl(unsigned long request, void *arg) const;
    void query_capabilities();
    void set_format();
    void set_framerate();
    void request_buffers();
    void map_buffers();
    void close();

    Config           cfg_;
    int              fd_       = -1;
    bool             mplane_   = false;
    enum v4l2_buf_type buf_type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int              cap_w_    = 0;
    int              cap_h_    = 0;
    int              n_bufs_   = 0;
    Buffer           bufs_[MAX_BUFS];
};
