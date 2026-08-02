#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "yolo11.h"
#include "image_utils.h"
#include "file_utils.h"
#include "image_drawing.h"

#if defined(ZERO_COPY)
#define MODE_TAG "[ZERO-COPY]"
#else
#define MODE_TAG "[NORMAL]"
#endif

static long get_current_time_us()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        printf("%s <model_path> <image_path>\n", argv[0]);
#if defined(ZERO_COPY)
        printf("Note: Running in ZERO-COPY mode (High Performance)\n");
#else
        printf("Note: Running in Normal mode (Standard)\n");
#endif
        return -1;
    }

    const char *model_path = argv[1];
    const char *image_path = argv[2];

    int ret;
    long init_start = 0, init_end = 0;
    long infer_start = 0, infer_end = 0;
    float infer_time_ms = 0.0f;
    rknn_app_context_t rknn_app_ctx;
    image_buffer_t src_image;
    object_detect_result_list od_results;

    memset(&rknn_app_ctx, 0, sizeof(rknn_app_context_t));
    memset(&src_image, 0, sizeof(image_buffer_t));
    memset(&od_results, 0, sizeof(object_detect_result_list));

    printf("%s ========================================\n", MODE_TAG);
    printf("%s YOLO11 Object Detection Demo\n", MODE_TAG);
#if defined(ZERO_COPY)
    printf("%s Mode: ZERO-COPY (NPU Shared Memory)\n", MODE_TAG);
#else
    printf("%s Mode: NORMAL (Standard Memory Copy)\n", MODE_TAG);
#endif
    printf("%s Model: %s\n", MODE_TAG, model_path);
    printf("%s Image: %s\n", MODE_TAG, image_path);
    printf("%s ========================================\n", MODE_TAG);

    init_post_process();

    init_start = get_current_time_us();
    ret = init_yolo11_model(model_path, &rknn_app_ctx);
    init_end = get_current_time_us();
    if (ret != 0)
    {
        printf("%s init_yolo11_model fail! ret=%d model_path=%s\n", MODE_TAG, ret, model_path);
        goto out;
    }
    printf("%s ✅ Model initialized in %.2f ms\n", MODE_TAG, (init_end - init_start) / 1000.0);

    ret = read_image(image_path, &src_image);
    if (ret != 0)
    {
        printf("%s read image fail! ret=%d image_path=%s\n", MODE_TAG, ret, image_path);
        goto out;
    }
    printf("%s 📷 Image loaded: %dx%d\n", MODE_TAG, src_image.width, src_image.height);

    infer_start = get_current_time_us();
    ret = inference_yolo11_model(&rknn_app_ctx, &src_image, &od_results);
    infer_end = get_current_time_us();
    if (ret != 0)
    {
        printf("%s inference_yolo11_model fail! ret=%d\n", MODE_TAG, ret);
        goto out;
    }

    infer_time_ms = (infer_end - infer_start) / 1000.0;
    printf("%s ⚡ Inference time: %.2f ms\n", MODE_TAG, infer_time_ms);
    printf("%s 🎯 Detected %d objects:\n", MODE_TAG, od_results.count);

    char text[256];
    for (int i = 0; i < od_results.count; i++)
    {
        object_detect_result *det_result = &(od_results.results[i]);
        printf("%s   [%d] %-20s @ (%4d %4d %4d %4d) %.3f\n",
               MODE_TAG,
               i + 1,
               coco_cls_to_name(det_result->cls_id),
               det_result->box.left, det_result->box.top,
               det_result->box.right, det_result->box.bottom,
               det_result->prop);

        int x1 = det_result->box.left;
        int y1 = det_result->box.top;
        int x2 = det_result->box.right;
        int y2 = det_result->box.bottom;
        sprintf(text, "%s %.1f%%", coco_cls_to_name(det_result->cls_id), det_result->prop * 100);
        draw_rectangle(&src_image, x1, y1, x2 - x1, y2 - y1, COLOR_RED, 3);
        draw_text(&src_image, text, x1, y1 - 20, COLOR_RED, 10);
    }

    ret = write_image("./yolo11_out.jpg", &src_image);
    if (ret != 0)
    {
        printf("%s write image fail! ret=%d\n", MODE_TAG, ret);
    }
    else
    {
        printf("%s 💾 Result saved to: ./yolo11_out.jpg\n", MODE_TAG);
    }

    printf("%s ========================================\n", MODE_TAG);
    printf("%s ✅ All done!\n", MODE_TAG);
    printf("%s ========================================\n", MODE_TAG);

out:
    deinit_post_process();
    release_yolo11_model(&rknn_app_ctx);
    if (src_image.virt_addr != NULL)
    {
        free(src_image.virt_addr);
    }

    return 0;
}