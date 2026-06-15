/*
 * V4L2 MJPEG capture -> decode -> display + H264 encode -> RTSP stream
 *
 * Pipeline:
 *   V4L2CaptureNode(MJPEG) --[Frame]--> [frame_to_packet] --[Packet]--> RKMPPDecoderNode(MJPEG)
 *                                                                                    |
 *                                                                   [DecodedFrame]---+-----> DRMDisplayNode
 *                                                                                    |
 *                                                                   NV12FrameToEncoderNode
 *                                                                          |
 *                                                                        [Frame]
 *                                                                          |
 *                                                                   FFmpegEncoderNode(H264) --[Packet]--> RTSPStreamerNode
 *
 * Usage:
 *   ./v4l2_mjpeg_dec_display_enc_rtsp [device] [rtsp_url] [drm_device]
 *   Defaults: /dev/video64, rtsp://192.168.42.110:8554/live, /dev/card0
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
#include "drm_display.h"
#include "ffmpeg_encoder_node.h"
#include "rtsp_streamer_node.h"
#include "pipeline.h"
#include "blocking_queue.h"
#include "common.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}

/* ── Frame -> Packet conversion helper ── */

/* Convert a Frame (MJPEG compressed data from V4L2) into a Packet (AVPacket for decoder).
 * Copies the MJPEG data into a new AVPacket. The original Frame's V4L2 buffer is
 * returned automatically when the Frame is destructed.
 * Returns an empty Packet on failure. */
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

/* ── Bridge thread: pop Frame from V4L2, convert to Packet, push to decoder ── */

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

        /* Frame destructor returns V4L2 buffer via release callback */
    }
    printf("[FrameToPacket] thread stopped\n");
}

/* ── Bridge node: DecodedFrame (NV12) -> Frame (NV12) for encoder ── */

class NV12FrameToEncoderNode : public NodeBase {
public:
    std::string name() const override { return "NV12FrameToEncoder"; }
    void init() override {}
    void run() override;

    void set_input(BlockingQueue<DecodedFrame> *q)  { input_ = q; }
    void set_output(BlockingQueue<Frame> *q)         { output_ = q; }

private:
    BlockingQueue<DecodedFrame> *input_  = nullptr;
    BlockingQueue<Frame>        *output_ = nullptr;
};

void NV12FrameToEncoderNode::run() {
    printf("[NV12FrameToEncoder] running\n");
    int64_t pts = 0;

    while (running_.load()) {
        DecodedFrame df;
        if (!input_->pop(df))
            break;

        if (!df.frame)
            continue;

        /* Transfer from GPU (DRM_PRIME) to CPU (NV12) if needed */
        AVFrame *sw_frame = av_frame_alloc();
        if (df.frame->format == AV_PIX_FMT_DRM_PRIME) {
            sw_frame->format = AV_PIX_FMT_NV12;
            int ret = av_hwframe_transfer_data(sw_frame, df.frame, 0);
            if (ret < 0) {
                fprintf(stderr, "[NV12FrameToEncoder] hwframe transfer failed\n");
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

        /* Allocate a contiguous NV12 buffer for the encoder */
        uint8_t *nv12_buf = (uint8_t *)malloc(y_size * 3 / 2);
        if (!nv12_buf) {
            av_frame_free(&sw_frame);
            continue;
        }

        /* Copy Y plane */
        for (int i = 0; i < h; i++)
            memcpy(nv12_buf + i * w,
                   sw_frame->data[0] + i * sw_frame->linesize[0], w);
        /* Copy UV plane */
        for (int i = 0; i < h / 2; i++)
            memcpy(nv12_buf + y_size + i * w,
                   sw_frame->data[1] + i * sw_frame->linesize[1], w);

        av_frame_free(&sw_frame);

        Frame frame;
        frame.data   = nv12_buf;
        frame.width  = w;
        frame.height = h;
        frame.size   = y_size * 3 / 2;
        frame.pts    = pts++;
        frame.release = [nv12_buf]() { free(nv12_buf); };

        if (!output_->push(std::move(frame))) {
            break;
        }
    }

    printf("[NV12FrameToEncoder] done\n");
}

/* ── Main ── */

static Pipeline *g_pipeline = nullptr;

static void signal_handler(int) {
    if (g_pipeline) g_pipeline->stop();
}

int main(int argc, char **argv)
{
    std::cout << "V4L2 MJPEG -> Decode -> Display + H264 Encode -> RTSP\n"
              << "\n"
              << "Usage: v4l2_mjpeg_dec_display_enc_rtsp [device] [rtsp_url] [drm_device]\n"
              << "\n"
              << "Arguments:\n"
              << "  device      V4L2 video device path   (default: /dev/video64)\n"
              << "  rtsp_url    RTSP push URL             (default: rtsp://192.168.42.110:8554/live)\n"
              << "  drm_device  DRM device path           (default: /dev/card0)\n"
              << std::endl;

    const char *device     = (argc > 1) ? argv[1] : "/dev/video64";
    const char *rtsp_url   = (argc > 2) ? argv[2] : "rtsp://192.168.42.110:8554/live";
    const char *drm_device = (argc > 3) ? argv[3] : "/dev/card0";

    std::cout << "Device: " << device
              << ", RTSP: " << rtsp_url
              << ", DRM: " << drm_device
              << std::endl;

    try {
        /* Create queues */
        BlockingQueue<Frame>         cap_to_f2p(4);     /* capture -> frame-to-packet thread */
        BlockingQueue<Packet>        f2p_to_dec(8);     /* frame-to-packet -> decoder */
        BlockingQueue<DecodedFrame>  dec_to_drm(4);     /* decoder -> display */
        BlockingQueue<DecodedFrame>  dec_to_bridge(4);  /* decoder -> encoder bridge */
        BlockingQueue<Frame>         bridge_to_enc(4);  /* bridge -> encoder */
        BlockingQueue<Packet>        enc_to_rtsp(8);    /* encoder -> rtsp */

        std::atomic<bool> f2p_running(true);

        /* Create nodes */
        V4L2CaptureNode::Config cap_cfg;
        cap_cfg.device = device;
        cap_cfg.pixfmt = V4L2_PIX_FMT_MJPEG;

        RKMPPDecoderNode::Config dec_cfg;
        dec_cfg.codec = RKMPPDecoderNode::MJPEG;

        DRMDisplayNode::Config drm_cfg;
        drm_cfg.device = drm_device;

        FFmpegEncoderNode::Config enc_cfg;
        enc_cfg.codec       = FFmpegEncoder::H264;
        enc_cfg.bitrate     = 4000000;
        enc_cfg.low_latency = true;

        RTSPStreamerNode::Config rtsp_cfg;
        rtsp_cfg.url = rtsp_url;

        auto cap_node    = std::make_shared<V4L2CaptureNode>(cap_cfg);
        auto dec_node    = std::make_shared<RKMPPDecoderNode>(dec_cfg);
        auto drm_node    = std::make_shared<DRMDisplayNode>(drm_cfg);
        auto bridge_node = std::make_shared<NV12FrameToEncoderNode>();
        auto enc_node    = std::make_shared<FFmpegEncoderNode>(enc_cfg);
        auto rtsp_node   = std::make_shared<RTSPStreamerNode>(rtsp_cfg);

        /* Wire up queues: V4L2 capture -> [frame_to_packet] -> decoder */
        cap_node->set_output(&cap_to_f2p);
        dec_node->set_input(&f2p_to_dec);

        /* Decoder fan-out: display + encoder bridge */
        dec_node->add_output(&dec_to_drm);
        dec_node->add_output(&dec_to_bridge);

        /* Display */
        drm_node->set_input(&dec_to_drm);

        /* Bridge: DecodedFrame -> Frame for encoder */
        bridge_node->set_input(&dec_to_bridge);
        bridge_node->set_output(&bridge_to_enc);

        /* Encoder */
        enc_node->set_input(&bridge_to_enc);
        enc_node->add_output(&enc_to_rtsp);

        /* RTSP */
        rtsp_node->set_input(&enc_to_rtsp);

        /* Build pipeline */
        Pipeline pipeline;
        pipeline.add_node(cap_node);
        pipeline.add_node(dec_node);
        pipeline.add_node(drm_node);
        pipeline.add_node(bridge_node);
        pipeline.add_node(enc_node);
        pipeline.add_node(rtsp_node);

        pipeline.register_closer([&cap_to_f2p]()   { cap_to_f2p.close(); });
        pipeline.register_closer([&f2p_to_dec]()    { f2p_to_dec.close(); });
        pipeline.register_closer([&dec_to_drm]()    { dec_to_drm.close(); });
        pipeline.register_closer([&dec_to_bridge]() { dec_to_bridge.close(); });
        pipeline.register_closer([&bridge_to_enc]() { bridge_to_enc.close(); });
        pipeline.register_closer([&enc_to_rtsp]()   { enc_to_rtsp.close(); });

        /* Install signal handler */
        g_pipeline = &pipeline;
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        /* Init in dependency order */
        cap_node->init();

        /* MJPEG decoder only needs codec type and resolution */
        dec_cfg.width  = cap_node->width();
        dec_cfg.height = cap_node->height();
        dec_node->init();

        drm_node->init();
        bridge_node->init();

        enc_node->set_resolution(cap_node->width(), cap_node->height());
        enc_node->init();

        rtsp_node->set_codec_ctx(enc_node->codec_ctx());

        /* Start pipeline nodes */
        pipeline.start();

        /* Start Frame->Packet bridge thread (outside pipeline) */
        std::thread f2p_thread(frame_to_packet_thread,
                               &cap_to_f2p, &f2p_to_dec, std::ref(f2p_running));

        /* Wait for completion */
        pipeline.wait();

        /* Stop bridge thread */
        f2p_running.store(false);
        cap_to_f2p.close();
        if (f2p_thread.joinable()) f2p_thread.join();

        printf("Done.\n");

    } catch (const std::exception &e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    return 0;
}
