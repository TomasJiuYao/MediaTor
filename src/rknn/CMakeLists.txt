set(RKNN_MODEL_ZOO_DIR ${CMAKE_SOURCE_DIR}/../../airockchip/rknn_model_zoo)

set(STB_INCLUDE_DIR ${RKNN_MODEL_ZOO_DIR}/3rdparty/stb_image)
set(LIBJPEG_INCLUDE_DIR ${RKNN_MODEL_ZOO_DIR}/3rdparty/jpeg_turbo/include)
set(LIBJPEG_AARCH64_DIR ${RKNN_MODEL_ZOO_DIR}/3rdparty/jpeg_turbo/Linux/aarch64)

set(LIBRGA_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/../../librga/include)

# ============================================================
# RKNN 推理模式选择：
#   OFF (默认): 普通模式 - 使用 malloc + 数据拷贝，简单易用
#   ON:         零拷贝模式 - 使用 NPU 共享内存，高性能低延迟
# ============================================================
option(RKNN_ZERO_COPY "Enable RKNN Zero-Copy mode for better performance" OFF)

if(RKNN_ZERO_COPY)
    message(STATUS "🚀 RKNN Mode: ZERO-COPY (High Performance)")
    add_library(medtor_rknn STATIC
        yolo11_zero_copy.cc
        postprocess.cc
        ${RKNN_MODEL_ZOO_DIR}/utils/file_utils.c
        ${RKNN_MODEL_ZOO_DIR}/utils/image_utils.c
        ${RKNN_MODEL_ZOO_DIR}/utils/image_drawing.c
    )
    target_compile_definitions(medtor_rknn PUBLIC ZERO_COPY)
else()
    message(STATUS "📦 RKNN Mode: Normal (Standard)")
    add_library(medtor_rknn STATIC
        yolo11.cc
        postprocess.cc
        ${RKNN_MODEL_ZOO_DIR}/utils/file_utils.c
        ${RKNN_MODEL_ZOO_DIR}/utils/image_utils.c
        ${RKNN_MODEL_ZOO_DIR}/utils/image_drawing.c
    )
endif()

target_include_directories(medtor_rknn PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/include/rknpu
    ${RKNN_MODEL_ZOO_DIR}/utils
    ${STB_INCLUDE_DIR}
    ${LIBJPEG_INCLUDE_DIR}
    ${LIBRGA_INCLUDE_DIR}
)

target_compile_definitions(medtor_rknn PRIVATE
    LIBRGA_IM2D_HANDLE
)

target_link_libraries(medtor_rknn PUBLIC
    rga
    ${LIBJPEG_AARCH64_DIR}/libturbojpeg.a
)