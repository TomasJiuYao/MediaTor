#pragma once

#include "node.h"
#include "blocking_queue.h"
#include "common.h"

#include <cstdio>

/* Writes decoded NV12 frames to a raw file.
 * Consumes DecodedFrame directly (MJPEG RKMPP decoder outputs NV12 without DRM). */

class NV12FileWriterNode : public NodeBase {
public:
    NV12FileWriterNode(const char *path, int max_frames = 0)
        : path_(path), max_frames_(max_frames) {}

    std::string name() const override { return "NV12FileWriter"; }
    void init() override;
    void run() override;

    void set_input(BlockingQueue<DecodedFrame> *q) { input_ = q; }

private:
    const char                  *path_;
    int                          max_frames_;
    FILE                        *fp_    = nullptr;
    BlockingQueue<DecodedFrame> *input_ = nullptr;
};
