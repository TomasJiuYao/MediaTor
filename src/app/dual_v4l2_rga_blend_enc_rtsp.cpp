/*
 * Dual V4L2 capture -> RGA blend -> H264 encode -> RTSP stream
 *
 * Pipeline:
 *   V4L2CaptureNode(MJPEG) --[Frame]--> [FrameToPacket] --[Packet]--> RKMPPDecoderNode(MJPEG)
 *                                                                          |
 *                                                               [DecodedFrame]
 *                                                                          |
 *                                                        NV12DecodedToRGANode
 *                                                                          |
 *                                                                     [RGAFrame] (bg)
 *                                                                          |
 *   V4L2CaptureNode(NV12)  --[Frame]--> NV12CaptureToRGANode --[RGAFrame] (fg)--+
 *                                                                          |
 *                                                          RGAProcessorNode(Blend)
 *                                                                          |
 *                                                                     [RGAFrame]
 *                                                                          |
 *                                                          RGAToEncoderNode
 *                                                                          |
 *                                                                        [Frame]
 *                                                                          |
 *                                                          FFmpegEncoderNode(H264) --[Packet]--> RTSPStreamerNode
 *
 * Usage:
 *   ./dual_v4l2_rga_blend_enc_rtsp [mjpeg_dev] [nv12_dev] [rtsp_url]
 *   Defaults: /dev/video21, /dev/video23, rtsp://192.168.42.110:8554/live
 */

#define _DEFAULT_SOURCE
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <signal.h>
#include <thread>

#include "v4l2_capture_node.h"
#include "rkmpp_decoder.h"
#include "ffmpeg_encoder_node.h"
#include "rtsp_streamer_node.h"
#include "rga_processor.h"
#include "pipeline.h"
#include "blocking_queue.h"
#include "common.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}

/* ── Frame -> Packet conversion helper ── */

static Packet frame_to_packet(Frame &frame) {
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) return Packet();

    if (av_new_packet(pkt, frame.size) < 0) {
        av_packet_free(&pkt);
        return Packet();
    }

    memcpy(pkt->data, frame.data, frame.size);
    pkt->pts = frame.pts;
    pkt->dts = frame.pts;

    return Packet(pkt);
}

/* ── Bridge thread: pop Frame from V4L2 MJPEG, convert to Packet, push to decoder ── */

static void frame_to_packet_thread(BlockingQueue<Frame> *in,
                                   BlockingQueue<Packet> *out,
                                   std::atomic<bool> &running)
{
    printf("[FrameToPacket] thread started\n");
    while (running.load()) {
        Frame frame;
        if (!in->pop(frame))
            break;

        if (!frame.data || frame.size <= 0)
            continue;

        Packet packet = frame_to_packet(frame);
        if (!packet.pkt)
            continue;

        if (!out->push(std::move(packet)))
            break;
    }
    printf("[FrameToPacket] thread stopped\n");
}

/* ── Bridge node: DecodedFrame (NV12 from MJPEG decoder) -> RGAFrame (bg for blend) ── */

class NV12DecodedToRGANode : public NodeBase {
public:
    std::string name() const override { return "NV12DecodedToRGA"; }
    void init() override {}
    void run() override;

    void set_input(BlockingQueue<DecodedFrame> *q)  { input_ = q; }
    void set_output(BlockingQueue<RGAFrame> *q)      { output_ = q; }

private:
    BlockingQueue<DecodedFrame> *input_  = nullptr;
    BlockingQueue<RGAFrame>     *output_ = nullptr;
};

void NV12DecodedToRGANode::run() {
    printf("[NV12DecodedToRGA] running\n");
    int64_t pts = 0;

    while (running_.load()) {
        DecodedFrame df;
        if (!input_->pop(df))
            break;

        if (!df.frame)
            continue;

        AVFrame *sw_frame = av_frame_alloc();
        if (df.frame->format == AV_PIX_FMT_DRM_PRIME) {
            sw_frame->format = AV_PIX_FMT_NV12;
            int ret = av_hwframe_transfer_data(sw_frame, df.frame, 0);
            if (ret < 0) {
                fprintf(stderr, "[NV12DecodedToRGA] hwframe transfer failed\n");
                av_frame_free(&sw_frame);
                continue;
            }
            sw_frame->width  = df.frame->width;
            sw_frame->height = df.frame->height;
        } else {
            av_frame_unref(sw_frame);
            av_frame_move_ref(sw_frame, df.frame);
        }

        int w = sw_frame->width;
        int h = sw_frame->height;
        int y_size = w * h;

        uint8_t *nv12_buf = (uint8_t *)malloc(y_size * 3 / 2);
        if (!nv12_buf) {
            av_frame_free(&sw_frame);
            continue;
        }

        for (int i = 0; i < h; i++)
            memcpy(nv12_buf + i * w,
                   sw_frame->data[0] + i * sw_frame->linesize[0], w);
        for (int i = 0; i < h / 2; i++)
            memcpy(nv12_buf + y_size + i * w,
                   sw_frame->data[1] + i * sw_frame->linesize[1], w);

        av_frame_free(&sw_frame);

        RGAFrame rga_frame;
        rga_frame.data   = nv12_buf;
        rga_frame.width  = w;
        rga_frame.height = h;
        rga_frame.format = RK_FORMAT_YCbCr_420_SP;
        rga_frame.size   = y_size * 3 / 2;
        rga_frame.pts    = pts++;
        rga_frame.fd     = -1;
        rga_frame.release = [nv12_buf]() { free(nv12_buf); };

        if (!output_->push(std::move(rga_frame)))
            break;
    }

    printf("[NV12DecodedToRGA] done\n");
}

/* ── Bridge node: Frame (NV12 from V4L2 capture) -> RGAFrame (fg for blend) ── */

class NV12CaptureToRGANode : public NodeBase {
public:
    std::string name() const override { return "NV12CaptureToRGA"; }
    void init() override {}
    void run() override;

    void set_input(BlockingQueue<Frame> *q)    { input_ = q; }
    void set_output(BlockingQueue<RGAFrame> *q) { output_ = q; }

private:
    BlockingQueue<Frame>    *input_  = nullptr;
    BlockingQueue<RGAFrame> *output_ = nullptr;
};

void NV12CaptureToRGANode::run() {
    printf("[NV12CaptureToRGA] running\n");
    int64_t pts = 0;

    while (running_.load()) {
        Frame frame;
        if (!input_->pop(frame))
            break;

        if (!frame.data)
            continue;

        int w = frame.width;
        int h = frame.height;
        int nv12_size = w * h * 3 / 2;

        uint8_t *nv12_buf = (uint8_t *)malloc(nv12_size);
        if (!nv12_buf)
            continue;

        int copy_size = frame.size > 0 ? frame.size : nv12_size;
        if (copy_size > nv12_size) copy_size = nv12_size;
        memcpy(nv12_buf, frame.data, copy_size);

        RGAFrame rga_frame;
        rga_frame.data   = nv12_buf;
        rga_frame.width  = w;
        rga_frame.height = h;
        rga_frame.format = RK_FORMAT_YCbCr_420_SP;
        rga_frame.size   = nv12_size;
        rga_frame.pts    = pts++;
        rga_frame.fd     = -1;
        rga_frame.release = [nv12_buf]() { free(nv12_buf); };

        if (!output_->push(std::move(rga_frame)))
            break;
    }

    printf("[NV12CaptureToRGA] done\n");
}

/* ── Bridge node: RGAFrame (NV12 from RGA blend output) -> Frame (NV12 for encoder) ── */

class RGAToEncoderNode : public NodeBase {
public:
    std::string name() const override { return "RGAToEncoder"; }
    void init() override {}
    void run() override;

    void set_input(BlockingQueue<RGAFrame> *q) { input_ = q; }
    void set_output(BlockingQueue<Frame> *q)    { output_ = q; }

private:
    BlockingQueue<RGAFrame> *input_  = nullptr;
    BlockingQueue<Frame>    *output_ = nullptr;
};

void RGAToEncoderNode::run() {
    printf("[RGAToEncoder] running\n");

    while (running_.load()) {
        RGAFrame rga_frame;
        if (!input_->pop(rga_frame))
            break;

        if (!rga_frame.data)
            continue;

        int w = rga_frame.width;
        int h = rga_frame.height;
        int nv12_size = w * h * 3 / 2;

        uint8_t *nv12_buf = (uint8_t *)malloc(nv12_size);
        if (!nv12_buf)
            continue;

        int copy_size = rga_frame.size > 0 ? rga_frame.size : nv12_size;
        if (copy_size > nv12_size) copy_size = nv12_size;
        memcpy(nv12_buf, rga_frame.data, copy_size);

        Frame frame;
        frame.data    = nv12_buf;
        frame.width   = w;
        frame.height  = h;
        frame.size    = nv12_size;
        frame.pts     = rga_frame.pts;
        frame.release = [nv12_buf]() { free(nv12_buf); };

        if (!output_->push(std::move(frame)))
            break;
    }

    printf("[RGAToEncoder] done\n");
}

/* ── Main ── */

static Pipeline *g_pipeline = nullptr;

static void signal_handler(int) {
    if (g_pipeline) g_pipeline->stop();
}

int main(int argc, char **argv)
{
    std::cout << "Dual V4L2 capture -> RGA blend -> H264 encode -> RTSP\n"
              << "\n"
              << "Usage: dual_v4l2_rga_blend_enc_rtsp [mjpeg_dev] [nv12_dev] [rtsp_url]\n"
              << "\n"
              << "Arguments:\n"
              << "  mjpeg_dev   V4L2 MJPEG device path  (default: /dev/video21)\n"
              << "  nv12_dev    V4L2 NV12 device path    (default: /dev/video23)\n"
              << "  rtsp_url    RTSP push URL            (default: rtsp://192.168.42.110:8554/live)\n"
              << std::endl;

    const char *mjpeg_dev = (argc > 1) ? argv[1] : "/dev/video21";
    const char *nv12_dev  = (argc > 2) ? argv[2] : "/dev/video23";
    const char *rtsp_url  = (argc > 3) ? argv[3] : "rtsp://192.168.5.100:8554/live";

    std::cout << "MJPEG device: " << mjpeg_dev
              << ", NV12 device: " << nv12_dev
              << ", RTSP: " << rtsp_url
              << std::endl;

    try {
        /* ── Queues ── */
        BlockingQueue<Frame>         mjpeg_cap_to_f2p(4);
        BlockingQueue<Packet>        f2p_to_dec(8);
        BlockingQueue<DecodedFrame>  dec_to_rga_bg(4);
        BlockingQueue<Frame>         nv12_cap_to_rga(4);
        BlockingQueue<RGAFrame>      rga_bg_queue(4);
        BlockingQueue<RGAFrame>      rga_fg_queue(4);
        BlockingQueue<RGAFrame>      rga_blend_out(4);
        BlockingQueue<Frame>         rga_to_enc(4);
        BlockingQueue<Packet>        enc_to_rtsp(8);

        std::atomic<bool> f2p_running(true);

        /* ── MJPEG capture node ── */
        V4L2CaptureNode::Config mjpeg_cap_cfg;
        mjpeg_cap_cfg.device = mjpeg_dev;
        mjpeg_cap_cfg.pixfmt = V4L2_PIX_FMT_MJPEG;
        mjpeg_cap_cfg.width = 1920;
        mjpeg_cap_cfg.height = 1080;

        /* ── NV12 capture node ── */
        V4L2CaptureNode::Config nv12_cap_cfg;
        nv12_cap_cfg.device = nv12_dev;
        nv12_cap_cfg.pixfmt = V4L2_PIX_FMT_NV12;
        nv12_cap_cfg.width = 640;
        nv12_cap_cfg.height = 320;

        /* ── MJPEG decoder ── */
        RKMPPDecoderNode::Config dec_cfg;
        dec_cfg.codec = RKMPPDecoderNode::MJPEG;

        /* ── RGA blend node ── */
        RGAProcessorNode::Config rga_cfg;
        rga_cfg.mode       = RGAProcessorNode::Mode::Blend;
        rga_cfg.dst_format = RK_FORMAT_YCbCr_420_SP;

        int bg_w = 1920, bg_h = 1080;
        int fg_w = 640, fg_h = 320;
        rga_cfg.overlay_x = (bg_w - fg_w) / 2;
        rga_cfg.overlay_y = (bg_h - fg_h) / 2;

        /* ── Encoder ── */
        FFmpegEncoderNode::Config enc_cfg;
        enc_cfg.codec       = FFmpegEncoder::H264;
        enc_cfg.bitrate     = 4000000;
        enc_cfg.low_latency = true;

        /* ── RTSP ── */
        RTSPStreamerNode::Config rtsp_cfg;
        rtsp_cfg.url = rtsp_url;

        /* ── Create nodes ── */
        auto mjpeg_cap_node = std::make_shared<V4L2CaptureNode>(mjpeg_cap_cfg);
        auto nv12_cap_node  = std::make_shared<V4L2CaptureNode>(nv12_cap_cfg);
        auto dec_node       = std::make_shared<RKMPPDecoderNode>(dec_cfg);
        auto dec_to_rga     = std::make_shared<NV12DecodedToRGANode>();
        auto cap_to_rga     = std::make_shared<NV12CaptureToRGANode>();
        auto rga_node       = std::make_shared<RGAProcessorNode>(rga_cfg);
        auto rga_to_enc_node = std::make_shared<RGAToEncoderNode>();
        auto enc_node       = std::make_shared<FFmpegEncoderNode>(enc_cfg);
        auto rtsp_node      = std::make_shared<RTSPStreamerNode>(rtsp_cfg);

        /* ── Wire up queues ── */

        /* MJPEG path: V4L2(MJPEG) -> [FrameToPacket] -> Decoder -> DecodedToRGA -> RGA(bg) */
        mjpeg_cap_node->set_output(&mjpeg_cap_to_f2p);
        dec_node->set_input(&f2p_to_dec);
        dec_node->add_output(&dec_to_rga_bg);
        dec_to_rga->set_input(&dec_to_rga_bg);
        dec_to_rga->set_output(&rga_bg_queue);

        /* NV12 path: V4L2(NV12) -> CaptureToRGA -> RGA(fg) */
        nv12_cap_node->set_output(&nv12_cap_to_rga);
        cap_to_rga->set_input(&nv12_cap_to_rga);
        cap_to_rga->set_output(&rga_fg_queue);

        /* RGA blend: bg + fg -> blend output */
        rga_node->set_input(&rga_bg_queue);
        rga_node->set_overlay(&rga_fg_queue);
        rga_node->set_output(&rga_blend_out);

        /* Blend output -> Encoder */
        rga_to_enc_node->set_input(&rga_blend_out);
        rga_to_enc_node->set_output(&rga_to_enc);

        /* Encoder -> RTSP */
        enc_node->set_input(&rga_to_enc);
        enc_node->add_output(&enc_to_rtsp);
        rtsp_node->set_input(&enc_to_rtsp);

        /* ── Build pipeline ── */
        Pipeline pipeline;
        pipeline.add_node(mjpeg_cap_node);
        pipeline.add_node(nv12_cap_node);
        pipeline.add_node(dec_node);
        pipeline.add_node(dec_to_rga);
        pipeline.add_node(cap_to_rga);
        pipeline.add_node(rga_node);
        pipeline.add_node(rga_to_enc_node);
        pipeline.add_node(enc_node);
        pipeline.add_node(rtsp_node);

        pipeline.register_closer([&mjpeg_cap_to_f2p]() { mjpeg_cap_to_f2p.close(); });
        pipeline.register_closer([&f2p_to_dec]()       { f2p_to_dec.close(); });
        pipeline.register_closer([&dec_to_rga_bg]()    { dec_to_rga_bg.close(); });
        pipeline.register_closer([&nv12_cap_to_rga]()  { nv12_cap_to_rga.close(); });
        pipeline.register_closer([&rga_bg_queue]()     { rga_bg_queue.close(); });
        pipeline.register_closer([&rga_fg_queue]()     { rga_fg_queue.close(); });
        pipeline.register_closer([&rga_blend_out]()    { rga_blend_out.close(); });
        pipeline.register_closer([&rga_to_enc]()       { rga_to_enc.close(); });
        pipeline.register_closer([&enc_to_rtsp]()      { enc_to_rtsp.close(); });

        /* ── Install signal handler ── */
        g_pipeline = &pipeline;
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        /* ── Init in dependency order ── */
        mjpeg_cap_node->init();
        nv12_cap_node->init();

        dec_cfg.width  = mjpeg_cap_node->width();
        dec_cfg.height = mjpeg_cap_node->height();
        dec_node->init();

        dec_to_rga->init();
        cap_to_rga->init();

        rga_node->init();
        rga_to_enc_node->init();

        enc_node->set_resolution(mjpeg_cap_node->width(), mjpeg_cap_node->height());
        enc_node->init();

        rtsp_node->set_codec_ctx(enc_node->codec_ctx());

        /* ── Start pipeline ── */
        pipeline.start();

        /* Start Frame->Packet bridge thread for MJPEG path */
        std::thread f2p_thread(frame_to_packet_thread,
                               &mjpeg_cap_to_f2p, &f2p_to_dec, std::ref(f2p_running));

        /* ── Wait for completion ── */
        pipeline.wait();

        f2p_running.store(false);
        mjpeg_cap_to_f2p.close();
        if (f2p_thread.joinable()) f2p_thread.join();

        printf("Done.\n");

    } catch (const std::exception &e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    return 0;
}