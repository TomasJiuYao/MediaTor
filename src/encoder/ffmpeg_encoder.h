#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

class FFmpegEncoder {
public:
    enum Codec { H264, H265 };

    /* Called for each encoded packet produced by encode_with_packets() */
    using PacketCallback = std::function<void(const AVPacket *pkt)>;

    struct Config {
        int      width     = 1920;
        int      height    = 1080;
        int      fps       = 30;
        int      bitrate   = 2000000;  /* 2 Mbps */
        int      gop_size  = 30;
        int      b_frames  = 0;        /* 0 = no B-frames for lowest latency */
        Codec    codec     = H264;     /* H264 or H265 */
        bool     low_latency = true;   /* zero-latency tuning */
    };

    FFmpegEncoder();
    ~FFmpegEncoder();

    FFmpegEncoder(const FFmpegEncoder &) = delete;
    FFmpegEncoder &operator=(const FFmpegEncoder &) = delete;

    void open(const Config &cfg);

    /* Encode a single NV12 buffer, calling cb for each output packet.
     * data points to the mmap'd V4L2 buffer (NV12 layout). */
    void encode_with_packets(const void *data, int64_t pts, PacketCallback cb);

    /* Flush the encoder */
    void flush();

    AVPixelFormat          pix_fmt() const;
    const AVCodecContext * codec_ctx() const;

private:
    static void fill_nv12_frame(AVFrame *frame, const void *data, int width, int height);
    void do_encode(AVFrame *frame, PacketCallback &cb);
    void close();

    Config          cfg_;
    bool            is_rkmpp_  = false;
    AVCodecContext *enc_ctx_   = nullptr;
    AVFrame        *frame_     = nullptr;
    AVFrame        *frame_yuv_ = nullptr;
    AVPacket       *pkt_       = nullptr;
    SwsContext     *sws_       = nullptr;
};
