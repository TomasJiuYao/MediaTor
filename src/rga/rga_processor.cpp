#include "rga_processor.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

RGAProcessorNode::~RGAProcessorNode() {}

const char *RGAProcessorNode::format_name(int fmt) {
    switch (fmt) {
    case RK_FORMAT_RGBA_8888:    return "RGBA_8888";
    case RK_FORMAT_RGBX_8888:    return "RGBX_8888";
    case RK_FORMAT_RGB_888:      return "RGB_888";
    case RK_FORMAT_BGRA_8888:    return "BGRA_8888";
    case RK_FORMAT_RGB_565:      return "RGB_565";
    case RK_FORMAT_YCbCr_422_SP: return "YCbCr_422_SP";
    case RK_FORMAT_YCbCr_422_P:  return "YCbCr_422_P";
    case RK_FORMAT_YCbCr_420_SP: return "YCbCr_420_SP(NV12)";
    case RK_FORMAT_YCbCr_420_P:  return "YCbCr_420_P(I420)";
    case RK_FORMAT_YCrCb_420_SP: return "YCrCb_420_SP(NV21)";
    case RK_FORMAT_YCrCb_420_P:  return "YCrCb_420_P";
    case RK_FORMAT_YUYV_422:     return "YUYV_422";
    case RK_FORMAT_UYVY_422:     return "UYVY_422";
    case RK_FORMAT_BGR_888:      return "BGR_888";
    case RK_FORMAT_BGRX_8888:    return "BGRX_8888";
    case RK_FORMAT_ARGB_8888:    return "ARGB_8888";
    case RK_FORMAT_ABGR_8888:    return "ABGR_8888";
    default:                     return "UNKNOWN";
    }
}

int RGAProcessorNode::format_bpp(int fmt) {
    switch (fmt) {
    case RK_FORMAT_RGBA_8888:
    case RK_FORMAT_RGBX_8888:
    case RK_FORMAT_BGRA_8888:
    case RK_FORMAT_BGRX_8888:
    case RK_FORMAT_ARGB_8888:
    case RK_FORMAT_ABGR_8888:
    case RK_FORMAT_XRGB_8888:
    case RK_FORMAT_XBGR_8888:
        return 4;
    case RK_FORMAT_RGB_888:
    case RK_FORMAT_BGR_888:
        return 3;
    case RK_FORMAT_RGB_565:
    case RK_FORMAT_BGR_565:
    case RK_FORMAT_RGBA_5551:
    case RK_FORMAT_RGBA_4444:
    case RK_FORMAT_YCbCr_422_SP:
    case RK_FORMAT_YCbCr_422_P:
    case RK_FORMAT_YCrCb_422_SP:
    case RK_FORMAT_YCrCb_422_P:
    case RK_FORMAT_YUYV_422:
    case RK_FORMAT_UYVY_422:
    case RK_FORMAT_YVYU_422:
    case RK_FORMAT_VYUY_422:
        return 2;
    case RK_FORMAT_YCbCr_420_SP:
    case RK_FORMAT_YCbCr_420_P:
    case RK_FORMAT_YCrCb_420_SP:
    case RK_FORMAT_YCrCb_420_P:
        return 2;
    case RK_FORMAT_YCbCr_400:
        return 1;
    default:
        return 4;
    }
}

void RGAProcessorNode::init() {
    printf("[RGAProcessor] mode=%s src_fmt=%s dst_fmt=%s dst_w=%d dst_h=%d overlay_x=%d overlay_y=%d\n",
           cfg_.mode == Mode::CvtColor  ? "CvtColor" :
           cfg_.mode == Mode::Resize    ? "Resize"   : "Blend",
           format_name(cfg_.src_format),
           format_name(cfg_.dst_format),
           cfg_.dst_width,
           cfg_.dst_height,
           cfg_.overlay_x,
           cfg_.overlay_y);
}

void RGAProcessorNode::run() {
    printf("[RGAProcessor] running\n");

    while (running_.load()) {
        RGAFrame in;
        if (!input_->pop(in))
            break;

        if (!in.data && in.fd < 0)
            continue;

        RGAFrame out;
        bool ok = false;

        switch (cfg_.mode) {
        case Mode::CvtColor:
            ok = process_cvtcolor(in, out);
            break;
        case Mode::Resize:
            ok = process_resize(in, out);
            break;
        case Mode::Blend:
            if (overlay_) {
                RGAFrame fg;
                if (overlay_->pop(fg)) {
                    ok = process_blend(in, fg, out);
                } else {
                    fprintf(stderr, "[RGAProcessor] overlay queue closed or empty, skip blend\n");
                }
            }
            break;
        }

        if (!ok)
            continue;

        if (output_) {
            if (!output_->push(std::move(out)))
                break;
        }
    }

    printf("[RGAProcessor] done\n");
}

void RGAProcessorNode::stop() {
    NodeBase::stop();
}

bool RGAProcessorNode::alloc_output_buffer(int width, int height, int format, RGAFrame &out) {
    int size = 0;

    switch (format) {
    case RK_FORMAT_YCbCr_420_SP:
    case RK_FORMAT_YCbCr_420_P:
    case RK_FORMAT_YCrCb_420_SP:
    case RK_FORMAT_YCrCb_420_P:
        size = width * height * 3 / 2;
        break;
    case RK_FORMAT_YCbCr_422_SP:
    case RK_FORMAT_YCbCr_422_P:
    case RK_FORMAT_YCrCb_422_SP:
    case RK_FORMAT_YCrCb_422_P:
        size = width * height * 2;
        break;
    default:
        size = width * height * format_bpp(format);
        break;
    }

    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf) {
        fprintf(stderr, "[RGAProcessor] alloc output buffer failed: %s\n", strerror(errno));
        return false;
    }
    memset(buf, 0, size);

    out.data    = buf;
    out.width   = width;
    out.height  = height;
    out.format  = format;
    out.size    = size;
    out.pts     = 0;
    out.fd      = -1;
    out.release = [buf]() { free(buf); };

    return true;
}

bool RGAProcessorNode::process_cvtcolor(RGAFrame &in, RGAFrame &out) {
    int dst_w = cfg_.dst_width  > 0 ? cfg_.dst_width  : in.width;
    int dst_h = cfg_.dst_height > 0 ? cfg_.dst_height : in.height;

    if (!alloc_output_buffer(dst_w, dst_h, cfg_.dst_format, out))
        return false;

    out.pts = in.pts;

    rga_buffer_t src = {};
    rga_buffer_t dst = {};

    if (in.fd >= 0) {
        src = wrapbuffer_fd(in.fd, in.width, in.height, in.format);
    } else {
        src = wrapbuffer_virtualaddr(in.data, in.width, in.height, in.format);
    }
    dst = wrapbuffer_virtualaddr(out.data, dst_w, dst_h, cfg_.dst_format);

    IM_STATUS status = imcvtcolor(src, dst, cfg_.src_format, cfg_.dst_format,
                                  cfg_.color_space_mode);
    if (status != IM_STATUS_SUCCESS) {
        fprintf(stderr, "[RGAProcessor] imcvtcolor failed: %s\n", imStrError(status));
        return false;
    }

    return true;
}

bool RGAProcessorNode::process_resize(RGAFrame &in, RGAFrame &out) {
    int dst_w = cfg_.dst_width;
    int dst_h = cfg_.dst_height;
    if (dst_w <= 0 || dst_h <= 0) {
        fprintf(stderr, "[RGAProcessor] Resize requires dst_width/dst_height > 0\n");
        return false;
    }

    int out_fmt = (cfg_.dst_format != 0) ? cfg_.dst_format : in.format;

    if (!alloc_output_buffer(dst_w, dst_h, out_fmt, out))
        return false;

    out.pts = in.pts;

    rga_buffer_t src = {};
    rga_buffer_t dst = {};

    if (in.fd >= 0) {
        src = wrapbuffer_fd(in.fd, in.width, in.height, in.format);
    } else {
        src = wrapbuffer_virtualaddr(in.data, in.width, in.height, in.format);
    }
    dst = wrapbuffer_virtualaddr(out.data, dst_w, dst_h, out_fmt);

    IM_STATUS status = imresize(src, dst);
    if (status != IM_STATUS_SUCCESS) {
        fprintf(stderr, "[RGAProcessor] imresize failed: %s\n", imStrError(status));
        return false;
    }

    return true;
}

bool RGAProcessorNode::process_blend(RGAFrame &bg, RGAFrame &fg, RGAFrame &out) {
    int dst_w = cfg_.dst_width  > 0 ? cfg_.dst_width  : bg.width;
    int dst_h = cfg_.dst_height > 0 ? cfg_.dst_height : bg.height;

    if (!alloc_output_buffer(dst_w, dst_h, cfg_.dst_format, out))
        return false;

    out.pts = bg.pts;

    rga_buffer_t src_bg = {};
    rga_buffer_t src_fg = {};
    rga_buffer_t dst    = {};

    if (bg.fd >= 0) {
        src_bg = wrapbuffer_fd(bg.fd, bg.width, bg.height, bg.format);
    } else {
        src_bg = wrapbuffer_virtualaddr(bg.data, bg.width, bg.height, bg.format);
    }

    if (fg.fd >= 0) {
        src_fg = wrapbuffer_fd(fg.fd, fg.width, fg.height, fg.format);
    } else {
        src_fg = wrapbuffer_virtualaddr(fg.data, fg.width, fg.height, fg.format);
    }

    dst = wrapbuffer_virtualaddr(out.data, dst_w, dst_h, cfg_.dst_format);

    int overlay_x = cfg_.overlay_x;
    int overlay_y = cfg_.overlay_y;

    IM_STATUS status;

    status = imcopy(src_bg, dst);
    if (status != IM_STATUS_SUCCESS) {
        fprintf(stderr, "[RGAProcessor] imcopy(bg->dst) failed: %s\n", imStrError(status));
        return false;
    }

    if (overlay_x == 0 && overlay_y == 0 && fg.width == dst_w && fg.height == dst_h) {
        status = imcopy(src_fg, dst);
        if (status != IM_STATUS_SUCCESS) {
            fprintf(stderr, "[RGAProcessor] imcopy(fg->dst) failed: %s\n", imStrError(status));
            return false;
        }
    } else {
        im_rect src_rect = {0, 0, fg.width, fg.height};
        im_rect dst_rect = {overlay_x, overlay_y, fg.width, fg.height};

        status = improcess(src_fg, dst, {},
                           src_rect, dst_rect, {},
                           -1, NULL, NULL,
                           IM_SYNC);
        if (status != IM_STATUS_SUCCESS) {
            fprintf(stderr, "[RGAProcessor] improcess(fg->dst overlay) failed: %s\n", imStrError(status));
            return false;
        }
    }

    return true;
}