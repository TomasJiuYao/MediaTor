#include "mjpeg_file_reader_node.h"

#include <cstdio>
#include <stdexcept>

void MjpegFileReaderNode::init() {
    int ret = avformat_open_input(&fmt_ctx_, path_, nullptr, nullptr);
    if (ret < 0)
        throw std::runtime_error(std::string("Cannot open '") + path_ + "'");

    ret = avformat_find_stream_info(fmt_ctx_, nullptr);
    if (ret < 0)
        throw std::runtime_error("Cannot find stream info");

    video_idx_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_idx_ < 0)
        throw std::runtime_error("No video stream found");

    AVStream *st = fmt_ctx_->streams[video_idx_];
    width_       = st->codecpar->width;
    height_      = st->codecpar->height;
    codec_params_ = st->codecpar;

    printf("[MjpegFileReader] %s: %dx%d, codec=%s\n",
           path_, width_, height_,
           avcodec_get_name(st->codecpar->codec_id));
}

void MjpegFileReaderNode::run() {
    printf("[MjpegFileReader] running\n");

    AVPacket *pkt = av_packet_alloc();
    int count = 0;

    while (running_.load()) {
        int ret = av_read_frame(fmt_ctx_, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF)
                printf("[MjpegFileReader] EOF\n");
            else
                fprintf(stderr, "[MjpegFileReader] read error\n");
            break;
        }

        if (pkt->stream_index != video_idx_) {
            av_packet_unref(pkt);
            continue;
        }

        for (auto *out : outputs_) {
            out->push(Packet(pkt));
        }
        av_packet_unref(pkt);

        count++;
        if (count % 30 == 0)
            printf("[MjpegFileReader] sent %d packets\n", count);
    }

    av_packet_free(&pkt);
    avformat_close_input(&fmt_ctx_);

    printf("[MjpegFileReader] done, %d packets sent\n", count);
}
