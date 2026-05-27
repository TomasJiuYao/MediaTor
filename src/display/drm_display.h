#pragma once

#include "node.h"
#include "blocking_queue.h"
#include "common.h"

#include <xf86drm.h>
#include <xf86drmMode.h>

#define DRM_BUF_COUNT 3

struct drm_vo_buf {
    int      fb_id;
    uint32_t handle;
    uint32_t size;
    uint32_t pitch;
    char    *map;
};

class DRMDisplayNode : public NodeBase {
public:
    struct Config {
        const char *device     = "/dev/card0";
        int         plane_type = DRM_PLANE_TYPE_PRIMARY;  /* PRIMARY or OVERLAY */
    };

    explicit DRMDisplayNode(const Config &cfg) : cfg_(cfg) {}
    ~DRMDisplayNode();

    std::string name() const override { return "DRMDisplay"; }
    void init() override;
    void run() override;
    void stop() override;

    void set_input(BlockingQueue<DecodedFrame> *q) { input_ = q; }

private:
    Config                       cfg_;
    int                          drm_fd_          = -1;
    drmModeCrtcPtr               crtc_            = nullptr;
    drmModePlanePtr              plane_           = nullptr;
    drmModeCrtcPtr               saved_crtc_      = nullptr;
    struct drm_vo_buf            buf_[DRM_BUF_COUNT] = {};
    int                          buf_idx_         = 0;
    int                          screen_w_        = 0;
    int                          screen_h_        = 0;
    bool                         buf_allocated_   = false;
    BlockingQueue<DecodedFrame> *input_           = nullptr;

    /* DRM helpers */
    bool setup_drm();
    bool alloc_buffers(int width, int height);
    void show_frame(AVFrame *sw_frame);
    void commit_buf(struct drm_vo_buf *b, int src_w, int src_h,
                    int crtc_x, int crtc_y, int crtc_w, int crtc_h);
};
