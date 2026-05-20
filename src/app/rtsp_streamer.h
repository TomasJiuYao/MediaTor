#pragma once

#include <cstdint>
#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

class RTSPStreamer {
public:
    struct Config {
        const char *url        = "rtsp://localhost:8554/test";
        int         width      = 1920;
        int         height     = 1080;
        int         fps        = 30;
        int         bitrate    = 4000000;
    };

    RTSPStreamer();
    ~RTSPStreamer();

    RTSPStreamer(const RTSPStreamer &) = delete;
    RTSPStreamer &operator=(const RTSPStreamer &) = delete;

    void open(const Config &cfg, const AVCodecContext *codec_ctx);
    void write_packet(const AVPacket *pkt);
    void close();

private:
    AVFormatContext    *fmt_ctx_   = nullptr;
    AVStream           *stream_    = nullptr;
    bool                opened_    = false;
};
