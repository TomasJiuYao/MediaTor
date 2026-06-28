#pragma once

#include <cstdint>
#include <functional>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
}

/* Frame passed from V4L2CaptureNode -> FFmpegEncoderNode */
struct Frame {
    void       *data   = nullptr;
    int         width   = 0;
    int         height  = 0;
    int         size    = 0;   /* data size in bytes (0 = compute from w*h for raw formats) */
    int64_t     pts     = 0;
    std::function<void()> release; /* re-queue V4L2 buffer on destruction */

    Frame() = default;

    ~Frame() {
        if (release) release();
    }

    /* move-only */
    Frame(Frame &&o) noexcept
        : data(o.data), width(o.width), height(o.height),
          size(o.size), pts(o.pts), release(std::move(o.release)) {
        o.data    = nullptr;
        o.release = nullptr;
    }

    Frame &operator=(Frame &&o) noexcept {
        if (this != &o) {
            if (release) release();
            data    = o.data;
            width   = o.width;
            height  = o.height;
            size    = o.size;
            pts     = o.pts;
            release = std::move(o.release);
            o.data    = nullptr;
            o.release = nullptr;
        }
        return *this;
    }

    Frame(const Frame &) = delete;
    Frame &operator=(const Frame &) = delete;
};

/* Packet passed from FFmpegEncoderNode -> RTSPStreamerNode / FileWriterNode */
struct Packet {
    AVPacket *pkt = nullptr;

    Packet() = default;

    explicit Packet(AVPacket *raw) : pkt(raw) {}

    explicit Packet(const AVPacket *p) {
        pkt = av_packet_alloc();
        av_packet_ref(pkt, p);
    }

    ~Packet() {
        if (pkt) av_packet_free(&pkt);
    }

    /* move-only */
    Packet(Packet &&o) noexcept : pkt(o.pkt) { o.pkt = nullptr; }

    Packet &operator=(Packet &&o) noexcept {
        if (this != &o) {
            if (pkt) av_packet_free(&pkt);
            pkt   = o.pkt;
            o.pkt = nullptr;
        }
        return *this;
    }

    Packet(const Packet &) = delete;
    Packet &operator=(const Packet &) = delete;
};

/* Decoded frame passed from RKMPPDecoderNode -> DRMDisplayNode */
struct DecodedFrame {
    AVFrame *frame = nullptr;  /* AV_PIX_FMT_DRM_PRIME */

    DecodedFrame() = default;

    explicit DecodedFrame(AVFrame *f) : frame(f) {}

    ~DecodedFrame() {
        if (frame) av_frame_free(&frame);
    }

    /* move-only */
    DecodedFrame(DecodedFrame &&o) noexcept : frame(o.frame) { o.frame = nullptr; }

    DecodedFrame &operator=(DecodedFrame &&o) noexcept {
        if (this != &o) {
            if (frame) av_frame_free(&frame);
            frame = o.frame;
            o.frame = nullptr;
        }
        return *this;
    }

    DecodedFrame(const DecodedFrame &) = delete;
    DecodedFrame &operator=(const DecodedFrame &) = delete;
};
