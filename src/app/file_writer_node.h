#pragma once

#include "node.h"
#include "blocking_queue.h"
#include "common.h"

#include <cstdio>

class FileWriterNode : public NodeBase {
public:
    explicit FileWriterNode(const char *path) : path_(path) {}

    std::string name() const override { return "FileWriter"; }
    void init() override;
    void run() override;

    void set_input(BlockingQueue<Packet> *q) { input_ = q; }

private:
    const char            *path_;
    FILE                  *fp_    = nullptr;
    BlockingQueue<Packet> *input_ = nullptr;
};
