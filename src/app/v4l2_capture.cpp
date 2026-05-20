#include "v4l2_capture.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdexcept>
#include <string>

V4L2Capture::V4L2Capture() = default;

V4L2Capture::~V4L2Capture() {
    close();
}

int V4L2Capture::xioctl(unsigned long request, void *arg) const {
    int r;
    do { r = ioctl(fd_, request, arg); } while (r == -1 && errno == EINTR);
    return r;
}

void V4L2Capture::open(const Config &cfg) {
    cfg_ = cfg;

    fd_ = ::open(cfg.device, O_RDWR | O_NONBLOCK);
    if (fd_ < 0)
        throw std::runtime_error(std::string("Cannot open ") + cfg.device + ": " + strerror(errno));

    query_capabilities();
    set_format();
    set_framerate();
    request_buffers();
    map_buffers();
}

void V4L2Capture::query_capabilities() {
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (xioctl(VIDIOC_QUERYCAP, &cap) < 0)
        throw std::runtime_error(std::string("VIDIOC_QUERYCAP: ") + strerror(errno));

    printf("V4L2: driver=%s card=%s bus=%s capabilities=0x%08x\n",
           cap.driver, cap.card, cap.bus_info, cap.capabilities);

    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
        printf("V4L2: using multi-plane capture\n");
        buf_type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        mplane_   = true;
    } else if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
        printf("V4L2: using single-plane capture\n");
        buf_type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        mplane_   = false;
    } else {
        throw std::runtime_error("Not a video capture device");
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING))
        throw std::runtime_error("Device does not support streaming");
}

void V4L2Capture::set_format() {
    if (mplane_) {
        struct v4l2_format fmt;
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt.fmt.pix_mp.width       = cfg_.width;
        fmt.fmt.pix_mp.height      = cfg_.height;
        fmt.fmt.pix_mp.pixelformat = cfg_.pixfmt;
        fmt.fmt.pix_mp.field       = V4L2_FIELD_NONE;
        fmt.fmt.pix_mp.num_planes  = FMT_NUM_PLANES;
        if (xioctl(VIDIOC_S_FMT, &fmt) < 0)
            throw std::runtime_error(std::string("VIDIOC_S_FMT (mplane): ") + strerror(errno));
        cap_w_ = fmt.fmt.pix_mp.width;
        cap_h_ = fmt.fmt.pix_mp.height;
        if (fmt.fmt.pix_mp.pixelformat != cfg_.pixfmt)
            throw std::runtime_error("Device did not accept pixel format");
    } else {
        struct v4l2_format fmt;
        memset(&fmt, 0, sizeof(fmt));
        fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width       = cfg_.width;
        fmt.fmt.pix.height      = cfg_.height;
        fmt.fmt.pix.pixelformat = cfg_.pixfmt;
        fmt.fmt.pix.field       = V4L2_FIELD_NONE;
        if (xioctl(VIDIOC_S_FMT, &fmt) < 0)
            throw std::runtime_error(std::string("VIDIOC_S_FMT: ") + strerror(errno));
        cap_w_ = fmt.fmt.pix.width;
        cap_h_ = fmt.fmt.pix.height;
        if (fmt.fmt.pix.pixelformat != cfg_.pixfmt)
            throw std::runtime_error("Device did not accept pixel format");
    }
    printf("V4L2: %dx%d format=0x%08x\n", cap_w_, cap_h_, cfg_.pixfmt);
}

void V4L2Capture::set_framerate() {
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = buf_type_;
    parm.parm.capture.timeperframe.numerator   = 1;
    parm.parm.capture.timeperframe.denominator = cfg_.fps;
    xioctl(VIDIOC_S_PARM, &parm);
}

void V4L2Capture::request_buffers() {
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count  = cfg_.num_bufs;
    req.type   = buf_type_;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(VIDIOC_REQBUFS, &req) < 0)
        throw std::runtime_error(std::string("VIDIOC_REQBUFS: ") + strerror(errno));
    if (req.count < 2)
        throw std::runtime_error("Insufficient buffer memory");
    n_bufs_ = req.count;
}

void V4L2Capture::map_buffers() {
    for (unsigned i = 0; i < (unsigned)n_bufs_; i++) {
        struct v4l2_plane planes[FMT_NUM_PLANES];
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = buf_type_;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (mplane_) {
            buf.m.planes = planes;
            buf.length   = FMT_NUM_PLANES;
        }
        if (xioctl(VIDIOC_QUERYBUF, &buf) < 0)
            throw std::runtime_error(std::string("VIDIOC_QUERYBUF: ") + strerror(errno));

        size_t length;
        off_t  offset;
        if (mplane_) {
            length = buf.m.planes[0].length;
            offset = buf.m.planes[0].m.mem_offset;
        } else {
            length = buf.length;
            offset = buf.m.offset;
        }

        bufs_[i].start  = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, offset);
        bufs_[i].length = length;
        if (bufs_[i].start == MAP_FAILED)
            throw std::runtime_error(std::string("mmap: ") + strerror(errno));
    }
}

void V4L2Capture::start_streaming() {
    for (int i = 0; i < n_bufs_; i++) {
        struct v4l2_plane planes[FMT_NUM_PLANES];
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = buf_type_;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (mplane_) {
            buf.m.planes = planes;
            buf.length   = FMT_NUM_PLANES;
        }
        if (xioctl(VIDIOC_QBUF, &buf) < 0)
            throw std::runtime_error(std::string("VIDIOC_QBUF: ") + strerror(errno));
    }
    if (xioctl(VIDIOC_STREAMON, &buf_type_) < 0)
        throw std::runtime_error(std::string("VIDIOC_STREAMON: ") + strerror(errno));
    printf("V4L2: streaming started\n");
}

void V4L2Capture::stop_streaming() {
    xioctl(VIDIOC_STREAMOFF, &buf_type_);
}

bool V4L2Capture::dequeue(struct v4l2_buffer &vbuf, struct v4l2_plane *planes) {
    memset(&vbuf, 0, sizeof(vbuf));
    vbuf.type   = buf_type_;
    vbuf.memory = V4L2_MEMORY_MMAP;
    if (mplane_) {
        vbuf.m.planes = planes;
        vbuf.length   = FMT_NUM_PLANES;
    }
    if (xioctl(VIDIOC_DQBUF, &vbuf) < 0) {
        if (errno == EAGAIN)
            return false;
        throw std::runtime_error(std::string("VIDIOC_DQBUF: ") + strerror(errno));
    }
    return true;
}

void V4L2Capture::enqueue(struct v4l2_buffer &vbuf) {
    if (xioctl(VIDIOC_QBUF, &vbuf) < 0)
        throw std::runtime_error(std::string("VIDIOC_QBUF: ") + strerror(errno));
}

const V4L2Capture::Buffer &V4L2Capture::buffer(int index) const { return bufs_[index]; }
int  V4L2Capture::width()     const { return cap_w_; }
int  V4L2Capture::height()    const { return cap_h_; }
bool V4L2Capture::mplane()    const { return mplane_; }

void V4L2Capture::close() {
    if (fd_ >= 0) {
        for (int i = 0; i < n_bufs_; i++)
            munmap(bufs_[i].start, bufs_[i].length);
        ::close(fd_);
        fd_ = -1;
    }
}
