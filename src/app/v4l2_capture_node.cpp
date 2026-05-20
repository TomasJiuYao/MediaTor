#include "v4l2_capture_node.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <sys/epoll.h>
#include <unistd.h>

void V4L2CaptureNode::init() {
    V4L2Capture::Config cap_cfg;
    cap_cfg.device   = cfg_.device;
    cap_cfg.width    = cfg_.width;
    cap_cfg.height   = cfg_.height;
    cap_cfg.pixfmt   = cfg_.pixfmt;
    cap_cfg.num_bufs = cfg_.num_bufs;
    cap_cfg.fps      = cfg_.fps;

    cap_.open(cap_cfg);
    cap_.start_streaming();

    /* Create epoll fd and add V4L2 fd */
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0)
        throw std::runtime_error(std::string("epoll_create1: ") + strerror(errno));

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events  = EPOLLIN;
    ev.data.fd = cap_.fd();
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, cap_.fd(), &ev) < 0)
        throw std::runtime_error(std::string("epoll_ctl ADD: ") + strerror(errno));
}

void V4L2CaptureNode::run() {
    printf("[V4L2Capture] running with epoll on fd=%d\n", cap_.fd());

    constexpr int MAX_EVENTS = 1;
    struct epoll_event events[MAX_EVENTS];
    int64_t pts = 0;

    while (running_.load()) {
        /* Wait up to 100ms for V4L2 fd to become readable */
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, 100);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "[V4L2Capture] epoll_wait error: %s\n", strerror(errno));
            break;
        }

        if (nfds == 0) continue; /* timeout, check running_ */

        /* V4L2 fd is readable — dequeue available frames */
        for (;;) {
            struct v4l2_plane planes[1];
            struct v4l2_buffer vbuf;
            if (!cap_.dequeue(vbuf, planes))
                break; /* EAGAIN: no more frames ready */

            Frame frame;
            frame.data   = cap_.buffer(vbuf.index).start;
            frame.width  = cap_.width();
            frame.height = cap_.height();
            frame.pts    = pts++;

            /* Capture index by value for the release callback */
            int buf_index = vbuf.index;
            V4L2Capture *cap_ptr = &cap_;
            frame.release = [cap_ptr, buf_index]() {
                cap_ptr->enqueue_by_index(buf_index);
            };

            if (!output_->push(std::move(frame))) {
                /* Queue closed, exit */
                return;
            }
        }
    }
}

void V4L2CaptureNode::stop() {
    NodeBase::stop();
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }
    cap_.stop_streaming();
}
