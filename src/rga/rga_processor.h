#pragma once

#include "node.h"
#include "blocking_queue.h"
#include "common.h"

#include <im2d.h>
#include <rga.h>

#include <cstdint>
#include <functional>
#include <string>

struct RGAFrame {
    void       *data   = nullptr;
    int         fd      = -1;
    int         width   = 0;
    int         height  = 0;
    int         format  = RK_FORMAT_YCbCr_420_SP;
    int         size    = 0;
    int64_t     pts     = 0;
    std::function<void()> release;

    RGAFrame() = default;

    ~RGAFrame() {
        if (release) release();
    }

    RGAFrame(RGAFrame &&o) noexcept
        : data(o.data), fd(o.fd), width(o.width), height(o.height),
          format(o.format), size(o.size), pts(o.pts), release(std::move(o.release)) {
        o.data    = nullptr;
        o.fd      = -1;
        o.release = nullptr;
    }

    RGAFrame &operator=(RGAFrame &&o) noexcept {
        if (this != &o) {
            if (release) release();
            data    = o.data;
            fd      = o.fd;
            width   = o.width;
            height  = o.height;
            format  = o.format;
            size    = o.size;
            pts     = o.pts;
            release = std::move(o.release);
            o.data    = nullptr;
            o.fd      = -1;
            o.release = nullptr;
        }
        return *this;
    }

    RGAFrame(const RGAFrame &) = delete;
    RGAFrame &operator=(const RGAFrame &) = delete;
};

class RGAProcessorNode : public NodeBase {
public:
    enum class Mode {
        CvtColor,
        Resize,
        Blend,
    };

    struct Config {
        Mode mode = Mode::CvtColor;

        int src_format = RK_FORMAT_YCbCr_420_SP;
        int dst_format = RK_FORMAT_RGBA_8888;

        int dst_width  = 0;
        int dst_height = 0;

        int color_space_mode = IM_COLOR_SPACE_DEFAULT;

        int blend_mode = IM_ALPHA_BLEND_SRC_OVER;

        int overlay_x = 0;
        int overlay_y = 0;
    };

    explicit RGAProcessorNode(const Config &cfg) : cfg_(cfg) {}
    ~RGAProcessorNode();

    std::string name() const override { return "RGAProcessor"; }
    void init() override;
    void run() override;
    void stop() override;

    void set_input(BlockingQueue<RGAFrame> *q) { input_ = q; }
    void set_overlay(BlockingQueue<RGAFrame> *q) { overlay_ = q; }
    void set_output(BlockingQueue<RGAFrame> *q) { output_ = q; }

    static const char *format_name(int fmt);
    static int format_bpp(int fmt);

private:
    Config                       cfg_;
    BlockingQueue<RGAFrame>     *input_   = nullptr;
    BlockingQueue<RGAFrame>     *overlay_ = nullptr;
    BlockingQueue<RGAFrame>     *output_  = nullptr;

    bool process_cvtcolor(RGAFrame &in, RGAFrame &out);
    bool process_resize(RGAFrame &in, RGAFrame &out);
    bool process_blend(RGAFrame &bg, RGAFrame &fg, RGAFrame &out);

    bool alloc_output_buffer(int width, int height, int format, RGAFrame &out);
};