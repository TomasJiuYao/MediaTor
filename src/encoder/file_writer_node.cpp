#include "file_writer_node.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <stdexcept>

void FileWriterNode::init() {
    fp_ = fopen(path_, "wb");
    if (!fp_)
        throw std::runtime_error(std::string("Cannot open ") + path_ + ": " + strerror(errno));
    printf("[FileWriter] writing to %s\n", path_);
}

void FileWriterNode::run() {
    printf("[FileWriter] running\n");

    int count = 0;
    while (running_.load()) {
        Packet packet;
        if (!input_->pop(packet))
            break; /* queue closed */

        if (packet.pkt) {
            fwrite(packet.pkt->data, 1, packet.pkt->size, fp_);
            count++;
            if (count % 30 == 0)
                printf("[FileWriter] wrote %d packets\n", count);
        }
    }

    if (fp_) {
        fclose(fp_);
        fp_ = nullptr;
    }
    printf("[FileWriter] done, %d packets written\n", count);
}
