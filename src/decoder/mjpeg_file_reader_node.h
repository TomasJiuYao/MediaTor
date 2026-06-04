#pragma once

#include "node.h"
#include "blocking_queue.h"
#include "common.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

/* Reads MJPEG file (AVI/MKV/raw) via avformat, outputs Packet to queue.
 * Also exposes the video stream's codec parameters so downstream decoders
 * can be configured correctly. */

class MjpegFileReaderNode : public NodeBase {
public:
    explicit MjpegFileReaderNode(const char *path) : path_(path) {}

    std::string name() const override { return "MjpegFileReader"; }
    void init() override;
    void run() override;

    void add_output(BlockingQueue<Packet> *q) { outputs_.push_back(q); }

    /* Access stream info after init() */
    int  width()      const { return width_; }
    int  height()     const { return height_; }
    const AVCodecParameters *codec_params() const { return codec_params_; }

private:
    const char                          *path_ = nullptr;
    AVFormatContext                      *fmt_ctx_      = nullptr;
    int                                  video_idx_     = -1;
    int                                  width_         = 0;
    int                                  height_        = 0;
    const AVCodecParameters             *codec_params_  = nullptr;
    std::vector<BlockingQueue<Packet>*>  outputs_;
};
