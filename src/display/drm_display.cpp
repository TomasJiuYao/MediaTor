#include "drm_display.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/imgutils.h>
}

#include <drm_fourcc.h>

/* ── DRM helper functions (modetest vo style) ── */

static drmModeConnectorPtr drmFoundConn(int fd, drmModeResPtr res)
{
    for (int i = 0; i < res->count_connectors; ++i) {
        drmModeConnectorPtr c = drmModeGetConnector(fd, res->connectors[i]);
        if (!c) continue;
        if (c->connector_type == DRM_MODE_CONNECTOR_HDMIA  ||
            c->connector_type == DRM_MODE_CONNECTOR_HDMIB  ||
            c->connector_type == DRM_MODE_CONNECTOR_DSI    ||
            c->connector_type == DRM_MODE_CONNECTOR_eDP    ||
            c->connector_type == DRM_MODE_CONNECTOR_DPI    ||
            c->connector_type == DRM_MODE_CONNECTOR_LVDS) {
            printf("[DRMDisplay] connector id %d\n", c->connector_id);
            return c;
        }
        drmModeFreeConnector(c);
    }
    return nullptr;
}

static drmModeCrtcPtr drmFoundCrtc(int fd, drmModeResPtr res,
                                    drmModeConnector *conn, int *crtc_index)
{
    int crtc_id = -1;
    for (int i = 0; i < res->count_encoders; i++) {
        drmModeEncoder *enc = drmModeGetEncoder(fd, res->encoders[i]);
        if (enc) {
            if (enc->encoder_id == conn->encoder_id) {
                crtc_id = enc->crtc_id;
                printf("[DRMDisplay] encoder id %d\n", enc->encoder_id);
                drmModeFreeEncoder(enc);
                break;
            }
            drmModeFreeEncoder(enc);
        }
    }

    if (crtc_id == -1) {
        uint32_t crtcs_for_connector = 0;
        for (int i = 0; i < conn->count_encoders; i++) {
            drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoders[i]);
            crtcs_for_connector |= enc->possible_crtcs;
            drmModeFreeEncoder(enc);
        }
        if (crtcs_for_connector != 0)
            crtc_id = res->crtcs[ffs(crtcs_for_connector) - 1];
    }
    if (crtc_id == -1) return nullptr;

    for (int i = 0; i < res->count_crtcs; i++) {
        drmModeCrtc *crtc = drmModeGetCrtc(fd, res->crtcs[i]);
        if (crtc) {
            if (crtc_id == crtc->crtc_id) {
                if (crtc_index) *crtc_index = i;
                return crtc;
            }
            drmModeFreeCrtc(crtc);
        }
    }
    return nullptr;
}

static int drmGetPlaneType(int fd, drmModePlanePtr p)
{
    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(
        fd, p->plane_id, DRM_MODE_OBJECT_PLANE);
    if (!props) return -errno;

    int type = -1;
    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyPtr prop = drmModeGetProperty(fd, props->props[i]);
        if (!strcmp(prop->name, "type")) {
            type = props->prop_values[i];
            drmModeFreeProperty(prop);
            break;
        }
        drmModeFreeProperty(prop);
    }
    drmModeFreeObjectProperties(props);
    return type;
}

static drmModePlanePtr drmGetPlaneByType(int fd, int crtc_index, int type)
{
    drmModePlaneResPtr plane_res = drmModeGetPlaneResources(fd);
    if (!plane_res) return nullptr;

    drmModePlanePtr plane = nullptr;
    for (uint32_t i = 0; i < plane_res->count_planes; i++) {
        drmModePlanePtr p = drmModeGetPlane(fd, plane_res->planes[i]);
        if (!(p->possible_crtcs & (1 << crtc_index))) {
            drmModeFreePlane(p);
            continue;
        }
        if (drmGetPlaneType(fd, p) == type) {
            plane = p;
            break;
        }
        drmModeFreePlane(p);
    }
    drmModeFreePlaneResources(plane_res);
    return plane;
}

static int drmCreateBufferNV12(int fd, int width, int height,
                                struct drm_vo_buf *buffer)
{
    struct drm_mode_create_dumb alloc_arg = {};
    struct drm_mode_map_dumb    mmap_arg  = {};

    if (fd < 0 || !width || !height) return -EINVAL;

    printf("[DRMDisplay] create NV12 buffer w:%d h:%d\n", width, height);

    alloc_arg.bpp    = 8;
    alloc_arg.width  = width;
    alloc_arg.height = height * 3 / 2;

    int ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &alloc_arg);
    if (ret) {
        fprintf(stderr, "[DRMDisplay] create dumb failed: %s\n", strerror(errno));
        return ret;
    }
    alloc_arg.pitch = width;

    uint32_t handles[4] = {}, pitches[4] = {}, offsets[4] = {};
    handles[0] = alloc_arg.handle;
    pitches[0] = alloc_arg.pitch;
    offsets[0] = 0;
    handles[1] = alloc_arg.handle;
    pitches[1] = pitches[0];
    offsets[1] = pitches[0] * height;

    ret = drmModeAddFB2(fd, width, height, DRM_FORMAT_NV12,
                         handles, pitches, offsets,
                         (uint32_t *)&buffer->fb_id, 0);
    if (ret) {
        fprintf(stderr, "[DRMDisplay] AddFB2 NV12 failed: %d\n", ret);
        goto destroy_dumb;
    }

    mmap_arg.handle = alloc_arg.handle;
    ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mmap_arg);
    if (ret) {
        fprintf(stderr, "[DRMDisplay] map dumb failed: %s\n", strerror(errno));
        goto destroy_dumb;
    }

    buffer->map = (char *)mmap(0, alloc_arg.size, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, mmap_arg.offset);
    if (buffer->map == MAP_FAILED) {
        fprintf(stderr, "[DRMDisplay] mmap failed: %s\n", strerror(errno));
        ret = -EINVAL;
        goto destroy_dumb;
    }

    buffer->handle = alloc_arg.handle;
    buffer->pitch  = alloc_arg.pitch;
    buffer->size   = alloc_arg.size;

    printf("[DRMDisplay] NV12 buffer created, size:%d fb_id:%d\n", buffer->size, buffer->fb_id);

destroy_dumb:
    {
        struct drm_mode_destroy_dumb destroy_arg = {};
        destroy_arg.handle = alloc_arg.handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
    }
    return ret;
}

static int drmCreateBufferARGB(int fd, int width, int height,
                                struct drm_vo_buf *buffer)
{
    struct drm_mode_create_dumb alloc_arg = {};
    struct drm_mode_map_dumb    mmap_arg  = {};

    if (fd < 0 || !width || !height) return -EINVAL;

    printf("[DRMDisplay] create ARGB buffer w:%d h:%d\n", width, height);

    alloc_arg.bpp    = 32;
    alloc_arg.width  = width;
    alloc_arg.height = height;

    int ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &alloc_arg);
    if (ret) {
        fprintf(stderr, "[DRMDisplay] create dumb failed: %s\n", strerror(errno));
        return ret;
    }

    buffer->pitch  = alloc_arg.pitch;
    buffer->size   = alloc_arg.size;
    buffer->handle = alloc_arg.handle;

    ret = drmModeAddFB(fd, width, height, 24, 32,
                       alloc_arg.pitch, alloc_arg.handle,
                       (uint32_t *)&buffer->fb_id);
    if (ret) {
        fprintf(stderr, "[DRMDisplay] AddFB ARGB failed: %d\n", ret);
        goto destroy_dumb;
    }

    mmap_arg.handle = alloc_arg.handle;
    ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mmap_arg);
    if (ret) {
        fprintf(stderr, "[DRMDisplay] map dumb failed: %s\n", strerror(errno));
        goto destroy_dumb;
    }

    buffer->map = (char *)mmap(0, alloc_arg.size, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, mmap_arg.offset);
    if (buffer->map == MAP_FAILED) {
        fprintf(stderr, "[DRMDisplay] mmap failed: %s\n", strerror(errno));
        ret = -EINVAL;
        goto destroy_dumb;
    }

    printf("[DRMDisplay] ARGB buffer created, size:%d pitch:%d fb_id:%d\n",
           buffer->size, buffer->pitch, buffer->fb_id);

destroy_dumb:
    {
        struct drm_mode_destroy_dumb destroy_arg = {};
        destroy_arg.handle = alloc_arg.handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
    }
    return ret;
}

static uint32_t get_property_id(int fd, drmModeObjectProperties *props,
                                 const char *name)
{
    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyPtr prop = drmModeGetProperty(fd, props->props[i]);
        if (!strcmp(prop->name, name)) {
            uint32_t id = prop->prop_id;
            drmModeFreeProperty(prop);
            return id;
        }
        drmModeFreeProperty(prop);
    }
    return 0;
}

/* ── DRMDisplayNode implementation ── */

DRMDisplayNode::~DRMDisplayNode() {
    stop();
}

bool DRMDisplayNode::setup_drm()
{
    drm_fd_ = open(cfg_.device, O_RDWR | O_CLOEXEC);
    if (drm_fd_ < 0)
        throw std::runtime_error(std::string("[DRMDisplay] open '") + cfg_.device + "' failed: " + strerror(errno));

    drmSetClientCap(drm_fd_, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmSetClientCap(drm_fd_, DRM_CLIENT_CAP_ATOMIC, 1);

    drmModeResPtr res = drmModeGetResources(drm_fd_);
    if (!res)
        throw std::runtime_error("[DRMDisplay] get resources failed");

    drmModeConnectorPtr conn = drmFoundConn(drm_fd_, res);
    if (!conn) {
        drmModeFreeResources(res);
        throw std::runtime_error("[DRMDisplay] no connector found");
    }

    conn_id_ = conn->connector_id;
    screen_w_ = conn->modes[0].hdisplay;
    screen_h_ = conn->modes[0].vdisplay;
    printf("[DRMDisplay] screen %dx%d conn_id:%d\n", screen_w_, screen_h_, conn_id_);

    int crtc_index = 0;
    crtc_ = drmFoundCrtc(drm_fd_, res, conn, &crtc_index);
    if (!crtc_) {
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        throw std::runtime_error("[DRMDisplay] no CRTC found");
    }

    saved_crtc_ = drmModeGetCrtc(drm_fd_, crtc_->crtc_id);

    if (cfg_.plane_type == DRM_PLANE_TYPE_OVERLAY) {
        primary_plane_ = drmGetPlaneByType(drm_fd_, crtc_index, DRM_PLANE_TYPE_PRIMARY);
        if (!primary_plane_) {
            drmModeFreeConnector(conn);
            drmModeFreeResources(res);
            throw std::runtime_error("[DRMDisplay] no PRIMARY plane found for overlay mode");
        }
        printf("[DRMDisplay] primary plane:%d\n", primary_plane_->plane_id);
    }

    plane_ = drmGetPlaneByType(drm_fd_, crtc_index, cfg_.plane_type);
    if (!plane_) {
        int fallback = (cfg_.plane_type == DRM_PLANE_TYPE_PRIMARY)
                        ? DRM_PLANE_TYPE_OVERLAY : DRM_PLANE_TYPE_PRIMARY;
        plane_ = drmGetPlaneByType(drm_fd_, crtc_index, fallback);
    }
    if (!plane_) {
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        throw std::runtime_error("[DRMDisplay] no suitable plane found");
    }

    printf("[DRMDisplay] CRTC:%d plane:%d\n", crtc_->crtc_id, plane_->plane_id);

    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    return true;
}

bool DRMDisplayNode::alloc_buffers(int width, int height)
{
    bool is_overlay = (cfg_.plane_type == DRM_PLANE_TYPE_OVERLAY);

    for (int i = 0; i < DRM_BUF_COUNT; i++) {
        int ret;
        if (is_overlay)
            ret = drmCreateBufferARGB(drm_fd_, width, height, &buf_[i]);
        else
            ret = drmCreateBufferNV12(drm_fd_, width, height, &buf_[i]);
        if (ret < 0) return false;
    }
    buf_allocated_ = true;
    return true;
}

void DRMDisplayNode::modeset_crtc()
{
    drmModeObjectProperties *props;
    uint32_t property_crtc_id, property_active, property_mode_id;
    uint32_t blob_id;

    props = drmModeObjectGetProperties(drm_fd_, conn_id_, DRM_MODE_OBJECT_CONNECTOR);
    property_crtc_id = get_property_id(drm_fd_, props, "CRTC_ID");
    drmModeFreeObjectProperties(props);

    props = drmModeObjectGetProperties(drm_fd_, crtc_->crtc_id, DRM_MODE_OBJECT_CRTC);
    property_active  = get_property_id(drm_fd_, props, "ACTIVE");
    property_mode_id = get_property_id(drm_fd_, props, "MODE_ID");
    drmModeFreeObjectProperties(props);

    drmModeConnectorPtr conn = drmModeGetConnector(drm_fd_, conn_id_);
    drmModeCreatePropertyBlob(drm_fd_, &conn->modes[0],
                              sizeof(conn->modes[0]), &blob_id);
    drmModeFreeConnector(conn);

    drmModeAtomicReq *req = drmModeAtomicAlloc();
    drmModeAtomicAddProperty(req, crtc_->crtc_id, property_active, 1);
    drmModeAtomicAddProperty(req, crtc_->crtc_id, property_mode_id, blob_id);
    drmModeAtomicAddProperty(req, conn_id_, property_crtc_id, crtc_->crtc_id);
    int ret = drmModeAtomicCommit(drm_fd_, req, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
    drmModeAtomicFree(req);
    drmModeDestroyPropertyBlob(drm_fd_, blob_id);

    if (ret)
        fprintf(stderr, "[DRMDisplay] modeset commit failed: %s\n", strerror(errno));
    else
        printf("[DRMDisplay] CRTC modeset done\n");

    modeset_done_ = true;
}

void DRMDisplayNode::setup_primary_plane()
{
    struct drm_mode_create_dumb create = {};
    struct drm_mode_map_dumb    map    = {};

    create.width  = screen_w_;
    create.height = screen_h_;
    create.bpp    = 32;
    int ret = drmIoctl(drm_fd_, DRM_IOCTL_MODE_CREATE_DUMB, &create);
    if (ret) {
        fprintf(stderr, "[DRMDisplay] primary create dumb failed: %s\n", strerror(errno));
        return;
    }

    primary_buf_.pitch  = create.pitch;
    primary_buf_.size   = create.size;
    primary_buf_.handle = create.handle;

    ret = drmModeAddFB(drm_fd_, screen_w_, screen_h_, 24, 32,
                       create.pitch, create.handle,
                       (uint32_t *)&primary_buf_.fb_id);
    if (ret) {
        fprintf(stderr, "[DRMDisplay] primary AddFB failed: %d\n", ret);
        goto destroy_dumb;
    }

    map.handle = create.handle;
    ret = drmIoctl(drm_fd_, DRM_IOCTL_MODE_MAP_DUMB, &map);
    if (ret) {
        fprintf(stderr, "[DRMDisplay] primary map dumb failed: %s\n", strerror(errno));
        goto destroy_dumb;
    }

    primary_buf_.map = (char *)mmap(0, create.size, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, drm_fd_, map.offset);
    if (primary_buf_.map == MAP_FAILED) {
        fprintf(stderr, "[DRMDisplay] primary mmap failed: %s\n", strerror(errno));
        goto destroy_dumb;
    }

    {
        auto abgr_to_argb = [](uint32_t abgr) -> uint32_t {
            uint32_t a = (abgr >> 24) & 0xFF;
            uint32_t b = (abgr >> 16) & 0xFF;
            uint32_t g = (abgr >> 8)  & 0xFF;
            uint32_t r =  abgr        & 0xFF;
            return (a << 24) | (r << 16) | (g << 8) | b;
        };

        uint32_t *px = reinterpret_cast<uint32_t *>(primary_buf_.map);
        FILE *fp = fopen("./Age4.abgr", "r");
        if (fp) {
            fread(px, primary_buf_.size, 1, fp);
            for (uint32_t i = 0; i < primary_buf_.size / 4; ++i)
                px[i] = abgr_to_argb(px[i]);
            fclose(fp);
        } else {
            for (uint32_t i = 0; i < primary_buf_.size / 4; ++i)
                px[i] = 0x000000FF;
        }
    }

    {
        drmModeObjectProperties *props = drmModeObjectGetProperties(
            drm_fd_, primary_plane_->plane_id, DRM_MODE_OBJECT_PLANE);

        uint32_t prop_crtc_id = get_property_id(drm_fd_, props, "CRTC_ID");
        uint32_t prop_fb_id   = get_property_id(drm_fd_, props, "FB_ID");
        uint32_t prop_crtc_x  = get_property_id(drm_fd_, props, "CRTC_X");
        uint32_t prop_crtc_y  = get_property_id(drm_fd_, props, "CRTC_Y");
        uint32_t prop_crtc_w  = get_property_id(drm_fd_, props, "CRTC_W");
        uint32_t prop_crtc_h  = get_property_id(drm_fd_, props, "CRTC_H");
        uint32_t prop_src_x   = get_property_id(drm_fd_, props, "SRC_X");
        uint32_t prop_src_y   = get_property_id(drm_fd_, props, "SRC_Y");
        uint32_t prop_src_w   = get_property_id(drm_fd_, props, "SRC_W");
        uint32_t prop_src_h   = get_property_id(drm_fd_, props, "SRC_H");
        drmModeFreeObjectProperties(props);

        drmModeAtomicReq *req = drmModeAtomicAlloc();
        drmModeAtomicAddProperty(req, primary_plane_->plane_id, prop_crtc_id, crtc_->crtc_id);
        drmModeAtomicAddProperty(req, primary_plane_->plane_id, prop_fb_id,   primary_buf_.fb_id);
        drmModeAtomicAddProperty(req, primary_plane_->plane_id, prop_crtc_x,  0);
        drmModeAtomicAddProperty(req, primary_plane_->plane_id, prop_crtc_y,  0);
        drmModeAtomicAddProperty(req, primary_plane_->plane_id, prop_crtc_w,  screen_w_);
        drmModeAtomicAddProperty(req, primary_plane_->plane_id, prop_crtc_h,  screen_h_);
        drmModeAtomicAddProperty(req, primary_plane_->plane_id, prop_src_x,   0);
        drmModeAtomicAddProperty(req, primary_plane_->plane_id, prop_src_y,   0);
        drmModeAtomicAddProperty(req, primary_plane_->plane_id, prop_src_w,   screen_w_ << 16);
        drmModeAtomicAddProperty(req, primary_plane_->plane_id, prop_src_h,   screen_h_ << 16);
        ret = drmModeAtomicCommit(drm_fd_, req, 0, nullptr);
        drmModeAtomicFree(req);

        if (ret)
            fprintf(stderr, "[DRMDisplay] primary plane commit failed: %s\n", strerror(errno));
        else
            printf("[DRMDisplay] primary plane set up (black %dx%d)\n", screen_w_, screen_h_);
    }

destroy_dumb:
    {
        struct drm_mode_destroy_dumb destroy = {};
        destroy.handle = create.handle;
        drmIoctl(drm_fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    }
}

void DRMDisplayNode::init_sws(int width, int height)
{
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
    if (bgra_frame_) {
        av_frame_free(&bgra_frame_);
        bgra_frame_ = nullptr;
    }

    sws_ctx_ = sws_getContext(width, height, AV_PIX_FMT_NV12,
                              width, height, AV_PIX_FMT_BGRA,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx_) {
        fprintf(stderr, "[DRMDisplay] sws_getContext failed\n");
        return;
    }

    bgra_frame_ = av_frame_alloc();
    bgra_frame_->format = AV_PIX_FMT_BGRA;
    bgra_frame_->width  = width;
    bgra_frame_->height = height;
    av_frame_get_buffer(bgra_frame_, 0);

    printf("[DRMDisplay] sws NV12->BGRA initialized (%dx%d)\n", width, height);
}

void DRMDisplayNode::commit_buf(struct drm_vo_buf *b, int src_w, int src_h,
                                 int crtc_x, int crtc_y, int crtc_w, int crtc_h)
{
    drmModeObjectProperties *props = drmModeObjectGetProperties(
        drm_fd_, plane_->plane_id, DRM_MODE_OBJECT_PLANE);

    uint32_t prop_crtc_id = get_property_id(drm_fd_, props, "CRTC_ID");
    uint32_t prop_fb_id   = get_property_id(drm_fd_, props, "FB_ID");
    uint32_t prop_crtc_x  = get_property_id(drm_fd_, props, "CRTC_X");
    uint32_t prop_crtc_y  = get_property_id(drm_fd_, props, "CRTC_Y");
    uint32_t prop_crtc_w  = get_property_id(drm_fd_, props, "CRTC_W");
    uint32_t prop_crtc_h  = get_property_id(drm_fd_, props, "CRTC_H");
    uint32_t prop_src_x   = get_property_id(drm_fd_, props, "SRC_X");
    uint32_t prop_src_y   = get_property_id(drm_fd_, props, "SRC_Y");
    uint32_t prop_src_w   = get_property_id(drm_fd_, props, "SRC_W");
    uint32_t prop_src_h   = get_property_id(drm_fd_, props, "SRC_H");
    drmModeFreeObjectProperties(props);

    drmModeAtomicReq *req = drmModeAtomicAlloc();
    drmModeAtomicAddProperty(req, plane_->plane_id, prop_crtc_id, crtc_->crtc_id);
    drmModeAtomicAddProperty(req, plane_->plane_id, prop_fb_id,   b->fb_id);
    drmModeAtomicAddProperty(req, plane_->plane_id, prop_crtc_x,  crtc_x);
    drmModeAtomicAddProperty(req, plane_->plane_id, prop_crtc_y,  crtc_y);
    drmModeAtomicAddProperty(req, plane_->plane_id, prop_crtc_w,  crtc_w);
    drmModeAtomicAddProperty(req, plane_->plane_id, prop_crtc_h,  crtc_h);
    drmModeAtomicAddProperty(req, plane_->plane_id, prop_src_x,   0);
    drmModeAtomicAddProperty(req, plane_->plane_id, prop_src_y,   0);
    drmModeAtomicAddProperty(req, plane_->plane_id, prop_src_w,   src_w << 16);
    drmModeAtomicAddProperty(req, plane_->plane_id, prop_src_h,   src_h << 16);

    int ret = drmModeAtomicCommit(drm_fd_, req, 0, nullptr);
    if (ret)
        fprintf(stderr, "[DRMDisplay] atomic commit failed: %s\n", strerror(errno));

    drmModeAtomicFree(req);
}

void DRMDisplayNode::show_frame(AVFrame *sw_frame)
{
    int w = sw_frame->width;
    int h = sw_frame->height;

    if (cfg_.plane_type == DRM_PLANE_TYPE_OVERLAY) {
        sws_scale(sws_ctx_,
                  sw_frame->data, sw_frame->linesize,
                  0, h,
                  bgra_frame_->data, bgra_frame_->linesize);

        char *dst = buf_[buf_idx_].map;
        int pitch = buf_[buf_idx_].pitch;

        for (int i = 0; i < h; i++) {
            memcpy(dst + i * pitch,
                   bgra_frame_->data[0] + i * bgra_frame_->linesize[0],
                   w * 4);
        }

        commit_buf(&buf_[buf_idx_], w, h, 0, 0, screen_w_ / 2, screen_h_ / 2);
    } else {
        char *dst = buf_[buf_idx_].map;

        for (int i = 0; i < h; i++) {
            memcpy(dst + i * w,
                   sw_frame->data[0] + i * sw_frame->linesize[0], w);
        }
        for (int i = 0; i < h / 2; i++) {
            memcpy(dst + w * h + i * w,
                   sw_frame->data[1] + i * sw_frame->linesize[1], w);
        }

        commit_buf(&buf_[buf_idx_], w, h, 0, 0, screen_w_, screen_h_);
    }

    buf_idx_ = (buf_idx_ + 1) % DRM_BUF_COUNT;
}

void DRMDisplayNode::init() {
    setup_drm();

    if (cfg_.plane_type == DRM_PLANE_TYPE_OVERLAY) {
        printf("need set crtc and plane?\n");
        // modeset_crtc();
        setup_primary_plane();
    }

    printf("[DRMDisplay] initialized on %s (plane_type=%s)\n",
           cfg_.device,
           cfg_.plane_type == DRM_PLANE_TYPE_OVERLAY ? "OVERLAY" : "PRIMARY");
}

void DRMDisplayNode::run() {
    printf("[DRMDisplay] running\n");

    AVFrame *sw_frame = av_frame_alloc();
    int frame_count = 0;

    while (running_.load()) {
        DecodedFrame df;
        if (!input_->pop(df))
            break;

        if (!df.frame)
            continue;

        if (df.frame->format == AV_PIX_FMT_DRM_PRIME) {
            sw_frame->format = AV_PIX_FMT_NV12;
            int ret = av_hwframe_transfer_data(sw_frame, df.frame, 0);
            if (ret < 0) {
                fprintf(stderr, "[DRMDisplay] hwframe transfer failed\n");
                continue;
            }
            sw_frame->width  = df.frame->width;
            sw_frame->height = df.frame->height;
        } else {
            av_frame_unref(sw_frame);
            av_frame_move_ref(sw_frame, df.frame);
        }

        if (!buf_allocated_) {
            if (!alloc_buffers(sw_frame->width, sw_frame->height)) {
                fprintf(stderr, "[DRMDisplay] buffer alloc failed\n");
                break;
            }
            if (cfg_.plane_type == DRM_PLANE_TYPE_OVERLAY)
                init_sws(sw_frame->width, sw_frame->height);
            printf("[DRMDisplay] first frame: %dx%d, buffers allocated\n",
                   sw_frame->width, sw_frame->height);
        }

        show_frame(sw_frame);

        frame_count++;

        av_frame_unref(sw_frame);
    }

    av_frame_free(&sw_frame);
    printf("[DRMDisplay] done, %d frames displayed\n", frame_count);
}

void DRMDisplayNode::stop() {
    NodeBase::stop();

    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
    if (bgra_frame_) {
        av_frame_free(&bgra_frame_);
        bgra_frame_ = nullptr;
    }

    if (saved_crtc_ && drm_fd_ >= 0) {
        drmModeConnector *conn = nullptr;
        drmModeRes *res = drmModeGetResources(drm_fd_);
        if (res) {
            for (int i = 0; i < res->count_connectors; i++) {
                conn = drmModeGetConnector(drm_fd_, res->connectors[i]);
                if (conn && conn->connection == DRM_MODE_CONNECTED)
                    break;
                drmModeFreeConnector(conn);
                conn = nullptr;
            }
        }
        if (conn) {
            drmModeSetCrtc(drm_fd_, saved_crtc_->crtc_id,
                           saved_crtc_->buffer_id,
                           saved_crtc_->x, saved_crtc_->y,
                           &conn->connector_id, 1, &saved_crtc_->mode);
            drmModeFreeConnector(conn);
        }
        if (res) drmModeFreeResources(res);
        drmModeFreeCrtc(saved_crtc_);
        saved_crtc_ = nullptr;
    }

    if (primary_buf_.map && primary_buf_.map != MAP_FAILED)
        munmap(primary_buf_.map, primary_buf_.size);
    if (primary_buf_.fb_id && drm_fd_ >= 0)
        drmModeRmFB(drm_fd_, primary_buf_.fb_id);
    primary_buf_ = {};

    for (int i = 0; i < DRM_BUF_COUNT; i++) {
        if (buf_[i].map && buf_[i].map != MAP_FAILED)
            munmap(buf_[i].map, buf_[i].size);
        if (buf_[i].fb_id && drm_fd_ >= 0)
            drmModeRmFB(drm_fd_, buf_[i].fb_id);
        buf_[i] = {};
    }
    buf_allocated_ = false;

    if (primary_plane_) { drmModeFreePlane(primary_plane_); primary_plane_ = nullptr; }
    if (plane_) { drmModeFreePlane(plane_); plane_ = nullptr; }
    if (crtc_)  { drmModeFreeCrtc(crtc_);  crtc_ = nullptr; }
    if (drm_fd_ >= 0) { close(drm_fd_); drm_fd_ = -1; }
}