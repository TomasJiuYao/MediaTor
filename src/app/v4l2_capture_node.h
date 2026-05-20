#pragma once

#include "node.h"
#include "blocking_queue.h"
#include "common.h"
#include "v4l2_capture.h"

#include <memory>

class V4L2CaptureNode : public NodeBase {
public:
    struct Config {
        const char *device    = "/dev/video55";
        int         width     = 1920;
        int         height    = 1080;
        uint32_t    pixfmt    = V4L2_PIX_FMT_NV12;
        int         num_bufs  = 4;
        int         fps       = 30;
    };

    explicit V4L2CaptureNode(const Config &cfg) : cfg_(cfg) {}

    std::string name() const override { return "V4L2Capture"; }
    void init() override;
    void run() override;
    void stop() override;

    void set_output(BlockingQueue<Frame> *q) { output_ = q; }

    /* Available after init() */
    int  width()  const { return cap_.width(); }
    int  height() const { return cap_.height(); }

private:
    Config                cfg_;
    V4L2Capture           cap_;
    BlockingQueue<Frame> *output_ = nullptr;
    int                   epoll_fd_ = -1;
};
