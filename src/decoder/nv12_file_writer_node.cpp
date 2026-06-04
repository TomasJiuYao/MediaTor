#include "nv12_file_writer_node.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <stdexcept>

extern "C" {
#include <libavutil/pixfmt.h>
#include <libavutil/imgutils.h>
}

void NV12FileWriterNode::init() {
    fp_ = fopen(path_, "wb");
    if (!fp_)
        throw std::runtime_error(std::string("Cannot open ") + path_ + ": " + strerror(errno));
    printf("[NV12FileWriter] writing to %s\n", path_);
}

void NV12FileWriterNode::run() {
    printf("[NV12FileWriter] running\n");

    int count = 0;
    int64_t file_size = 0;

    while (running_.load()) {
        if (max_frames_ > 0 && count >= max_frames_)
            break;

        DecodedFrame df;
        if (!input_->pop(df))
            break; /* queue closed */

        AVFrame *avf = df.frame;
        if (!avf) continue;

        int w = avf->width;
        int h = avf->height;

        if (avf->format == AV_PIX_FMT_NV12) {
            /* Direct NV12: write Y + UV planes */
            for (int i = 0; i < h; i++)
                fwrite(avf->data[0] + i * avf->linesize[0], 1, w, fp_);
            for (int i = 0; i < h / 2; i++)
                fwrite(avf->data[1] + i * avf->linesize[1], 1, w, fp_);
        } else if (avf->format == AV_PIX_FMT_YUV420P) {
            /* YUV420P -> NV12: interleave U and V */
            uint8_t *uv_row = static_cast<uint8_t *>(aligned_alloc(32, w));
            for (int i = 0; i < h; i++)
                fwrite(avf->data[0] + i * avf->linesize[0], 1, w, fp_);
            for (int i = 0; i < h / 2; i++) {
                for (int j = 0; j < w / 2; j++) {
                    uv_row[j * 2]     = avf->data[1][i * avf->linesize[1] + j]; /* U */
                    uv_row[j * 2 + 1] = avf->data[2][i * avf->linesize[2] + j]; /* V */
                }
                fwrite(uv_row, 1, w, fp_);
            }
            free(uv_row);
        } else if (avf->format == AV_PIX_FMT_DRM_PRIME) {
            /* DRM_PRIME: transfer to software NV12 then write */
            AVFrame *sw_frame = av_frame_alloc();
            sw_frame->format = AV_PIX_FMT_NV12;
            int ret = av_hwframe_transfer_data(sw_frame, avf, 0);
            if (ret < 0) {
                fprintf(stderr, "[NV12FileWriter] hwframe transfer failed, skipping\n");
                av_frame_free(&sw_frame);
                continue;
            }
            sw_frame->width  = w;
            sw_frame->height = h;
            for (int i = 0; i < h; i++)
                fwrite(sw_frame->data[0] + i * sw_frame->linesize[0], 1, w, fp_);
            for (int i = 0; i < h / 2; i++)
                fwrite(sw_frame->data[1] + i * sw_frame->linesize[1], 1, w, fp_);
            av_frame_free(&sw_frame);
        } else {
            fprintf(stderr, "[NV12FileWriter] unsupported pixel format %d, skipping\n",
                    avf->format);
            continue;
        }

        file_size += w * h * 3 / 2;
        count++;

        if (count % 30 == 0)
            printf("[NV12FileWriter] wrote %d frames, %.1f MB\n",
                   count, file_size / 1024.0 / 1024.0);
    }

    if (fp_) {
        fclose(fp_);
        fp_ = nullptr;
    }
    printf("[NV12FileWriter] done, %d frames written (%.1f MB)\n",
           count, file_size / 1024.0 / 1024.0);
}
