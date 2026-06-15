#include "v4l2_capture_node.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <stdexcept>

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

    /* Register V4L2 fd for readable events */
    poller_.add(cap_.fd(), EPOLLIN);
}

void V4L2CaptureNode::run() {
    printf("[V4L2Capture] running with epoll on fd=%d\n", cap_.fd());

    constexpr int MAX_EVENTS = 1;
    struct epoll_event events[MAX_EVENTS];
    int64_t pts = 0;

    while (running_.load()) {
        /* Wait up to 100ms for V4L2 fd to become readable */
        int nfds = poller_.wait(events, MAX_EVENTS, 100);
        if (nfds < 0) {
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
            frame.size   = cap_.mplane() ? planes[0].bytesused : vbuf.bytesused;
            if (frame.size == 0)
                frame.size = cap_.width() * cap_.height() * 3 / 2;
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
    poller_.remove(cap_.fd());
    cap_.stop_streaming();
}
