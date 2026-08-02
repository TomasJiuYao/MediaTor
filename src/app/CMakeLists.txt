add_executable(v4l2_rkmpp_enc
    v4l2_rkmpp_enc.cpp
)

target_include_directories(v4l2_rkmpp_enc PRIVATE
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/v4l2
    ${CMAKE_SOURCE_DIR}/src/encoder
    ${CMAKE_SOURCE_DIR}/src/rtsp
)

# FFmpeg static libs must be linked in dependency order (dependent first):
#   avformat → avcodec, avdevice, avfilter, swresample → avutil
#   avdevice → avformat, swresample → avutil
#   avfilter → avformat, avcodec, swresample → avutil
#   avcodec  → swresample, swscale → avutil
#   swscale  → avutil
#   postproc → avutil
target_link_libraries(v4l2_rkmpp_enc PRIVATE
    medtor_v4l2
    medtor_encoder
    medtor_rtsp
    medtor_common
    ${FFMPEG_LIB_DIR}/libavformat.a
    ${FFMPEG_LIB_DIR}/libavdevice.a
    ${FFMPEG_LIB_DIR}/libavfilter.a
    ${FFMPEG_LIB_DIR}/libavcodec.a
    ${FFMPEG_LIB_DIR}/libswscale.a
    ${FFMPEG_LIB_DIR}/libswresample.a
    ${FFMPEG_LIB_DIR}/libpostproc.a
    ${FFMPEG_LIB_DIR}/libavutil.a
    rockchip_mpp
    drm
    rga
    m
    pthread
    dl
    z
    bz2
)

# HTTP demo server based on workflow
add_executable(http_demo_server
    http_demo_server.cpp
)

target_include_directories(http_demo_server PRIVATE
    ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(http_demo_server PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/workflow/libworkflow.a
    ssl
    crypto
    m
    pthread
    dl
    z
    bz2
)

# RKMPP MJPEG decode demo (standalone C)
add_executable(rkmpp_mjpg_dec
    rkmpp_mjpg_dec.c
)

target_include_directories(rkmpp_mjpg_dec PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${TOOLCHAIN_SYSROOT}/usr/include/libdrm
)

target_link_libraries(rkmpp_mjpg_dec PRIVATE
    ${FFMPEG_LIB_DIR}/libavformat.a
    ${FFMPEG_LIB_DIR}/libavdevice.a
    ${FFMPEG_LIB_DIR}/libavfilter.a
    ${FFMPEG_LIB_DIR}/libavcodec.a
    ${FFMPEG_LIB_DIR}/libswscale.a
    ${FFMPEG_LIB_DIR}/libswresample.a
    ${FFMPEG_LIB_DIR}/libpostproc.a
    ${FFMPEG_LIB_DIR}/libavutil.a
    rockchip_mpp
    drm
    rga
    m
    pthread
    dl
    z
    bz2
)

# RTSP pull -> RKMPP decode -> DRM display
add_executable(rtsp_pull_decode_display
    rtsp_pull_decode_display.cpp
)

target_include_directories(rtsp_pull_decode_display PRIVATE
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/decoder
    ${CMAKE_SOURCE_DIR}/src/display
)

target_link_libraries(rtsp_pull_decode_display PRIVATE
    medtor_decoder
    medtor_display
    medtor_common
    ${FFMPEG_LIB_DIR}/libavformat.a
    ${FFMPEG_LIB_DIR}/libavdevice.a
    ${FFMPEG_LIB_DIR}/libavfilter.a
    ${FFMPEG_LIB_DIR}/libavcodec.a
    ${FFMPEG_LIB_DIR}/libswscale.a
    ${FFMPEG_LIB_DIR}/libswresample.a
    ${FFMPEG_LIB_DIR}/libpostproc.a
    ${FFMPEG_LIB_DIR}/libavutil.a
    rockchip_mpp
    drm
    rga
    m
    pthread
    dl
    z
    bz2
)

# RTSP pull -> RKMPP decode -> NV12 file save
add_executable(rtsp_pull_decode_nv12
    rtsp_pull_decode_nv12.cpp
)

target_include_directories(rtsp_pull_decode_nv12 PRIVATE
    ${CMAKE_SOURCE_DIR}/src/common
    ${FFMPEG_INCLUDE_DIR}
    ${TOOLCHAIN_SYSROOT}/usr/include/libdrm
)

target_link_libraries(rtsp_pull_decode_nv12 PRIVATE
    ${FFMPEG_LIB_DIR}/libavformat.a
    ${FFMPEG_LIB_DIR}/libavdevice.a
    ${FFMPEG_LIB_DIR}/libavfilter.a
    ${FFMPEG_LIB_DIR}/libavcodec.a
    ${FFMPEG_LIB_DIR}/libswscale.a
    ${FFMPEG_LIB_DIR}/libswresample.a
    ${FFMPEG_LIB_DIR}/libpostproc.a
    ${FFMPEG_LIB_DIR}/libavutil.a
    rockchip_mpp
    drm
    rga
    m
    pthread
    dl
    z
    bz2
)

# V4L2 capture -> RKMPP MJPEG encode (pipeline)
add_executable(v4l2_rkmpp_enc_mjpg
    v4l2_rkmpp_enc_mjpg.cpp
)

target_include_directories(v4l2_rkmpp_enc_mjpg PRIVATE
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/v4l2
    ${CMAKE_SOURCE_DIR}/src/encoder
    ${CMAKE_SOURCE_DIR}/src/rtsp
)

target_link_libraries(v4l2_rkmpp_enc_mjpg PRIVATE
    medtor_v4l2
    medtor_encoder
    medtor_rtsp
    medtor_common
    ${FFMPEG_LIB_DIR}/libavformat.a
    ${FFMPEG_LIB_DIR}/libavdevice.a
    ${FFMPEG_LIB_DIR}/libavfilter.a
    ${FFMPEG_LIB_DIR}/libavcodec.a
    ${FFMPEG_LIB_DIR}/libswscale.a
    ${FFMPEG_LIB_DIR}/libswresample.a
    ${FFMPEG_LIB_DIR}/libpostproc.a
    ${FFMPEG_LIB_DIR}/libavutil.a
    rockchip_mpp
    drm
    rga
    m
    pthread
    dl
    z
    bz2
)

# V4L2 capture -> MJPEG encode -> MJPEG decode -> NV12 file save (pipeline)
add_executable(v4l2_rkmpp_enc_dec
    v4l2_rkmpp_enc_dec.cpp
)

target_include_directories(v4l2_rkmpp_enc_dec PRIVATE
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/v4l2
    ${CMAKE_SOURCE_DIR}/src/encoder
    ${CMAKE_SOURCE_DIR}/src/decoder
)

target_link_libraries(v4l2_rkmpp_enc_dec PRIVATE
    medtor_v4l2
    medtor_encoder
    medtor_decoder
    medtor_common
    ${FFMPEG_LIB_DIR}/libavformat.a
    ${FFMPEG_LIB_DIR}/libavdevice.a
    ${FFMPEG_LIB_DIR}/libavfilter.a
    ${FFMPEG_LIB_DIR}/libavcodec.a
    ${FFMPEG_LIB_DIR}/libswscale.a
    ${FFMPEG_LIB_DIR}/libswresample.a
    ${FFMPEG_LIB_DIR}/libpostproc.a
    ${FFMPEG_LIB_DIR}/libavutil.a
    rockchip_mpp
    drm
    rga
    m
    pthread
    dl
    z
    bz2
)

# MJPEG file -> RKMPP decode -> NV12 file save (pipeline)
add_executable(rkmpp_mjpg_dec_pipe
    rkmpp_mjpg_dec_pipe.cpp
)

target_include_directories(rkmpp_mjpg_dec_pipe PRIVATE
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/decoder
)

target_link_libraries(rkmpp_mjpg_dec_pipe PRIVATE
    medtor_decoder
    medtor_common
    ${FFMPEG_LIB_DIR}/libavformat.a
    ${FFMPEG_LIB_DIR}/libavdevice.a
    ${FFMPEG_LIB_DIR}/libavfilter.a
    ${FFMPEG_LIB_DIR}/libavcodec.a
    ${FFMPEG_LIB_DIR}/libswscale.a
    ${FFMPEG_LIB_DIR}/libswresample.a
    ${FFMPEG_LIB_DIR}/libpostproc.a
    ${FFMPEG_LIB_DIR}/libavutil.a
    rockchip_mpp
    drm
    rga
    m
    pthread
    dl
    z
    bz2
)

# RTSP pull -> RKMPP decode -> DRM display (modetest vo style, pipeline)
add_executable(rtsp_pull_decode_drm
    rtsp_pull_decode_drm.cpp
)

target_include_directories(rtsp_pull_decode_drm PRIVATE
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/decoder
    ${CMAKE_SOURCE_DIR}/src/display
)

target_link_libraries(rtsp_pull_decode_drm PRIVATE
    medtor_decoder
    medtor_display
    medtor_common
    ${FFMPEG_LIB_DIR}/libavformat.a
    ${FFMPEG_LIB_DIR}/libavdevice.a
    ${FFMPEG_LIB_DIR}/libavfilter.a
    ${FFMPEG_LIB_DIR}/libavcodec.a
    ${FFMPEG_LIB_DIR}/libswscale.a
    ${FFMPEG_LIB_DIR}/libswresample.a
    ${FFMPEG_LIB_DIR}/libpostproc.a
    ${FFMPEG_LIB_DIR}/libavutil.a
    rockchip_mpp
    drm
    rga
    m
    pthread
    dl
    z
    bz2
)

# V4L2 MJPEG capture -> decode -> display + H264 encode -> RTSP stream
add_executable(v4l2_mjpeg_dec_display_enc_rtsp
    v4l2_mjpeg_dec_display_enc_rtsp.cpp
)

target_include_directories(v4l2_mjpeg_dec_display_enc_rtsp PRIVATE
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/v4l2
    ${CMAKE_SOURCE_DIR}/src/decoder
    ${CMAKE_SOURCE_DIR}/src/display
    ${CMAKE_SOURCE_DIR}/src/encoder
    ${CMAKE_SOURCE_DIR}/src/rtsp
)

target_link_libraries(v4l2_mjpeg_dec_display_enc_rtsp PRIVATE
    medtor_v4l2
    medtor_decoder
    medtor_display
    medtor_encoder
    medtor_rtsp
    medtor_common
    ${FFMPEG_LIB_DIR}/libavformat.a
    ${FFMPEG_LIB_DIR}/libavdevice.a
    ${FFMPEG_LIB_DIR}/libavfilter.a
    ${FFMPEG_LIB_DIR}/libavcodec.a
    ${FFMPEG_LIB_DIR}/libswscale.a
    ${FFMPEG_LIB_DIR}/libswresample.a
    ${FFMPEG_LIB_DIR}/libpostproc.a
    ${FFMPEG_LIB_DIR}/libavutil.a
    rockchip_mpp
    drm
    rga
    m
    pthread
    dl
    z
    bz2
)

# Dual V4L2 capture (MJPEG+NV12) -> RGA blend -> H264 encode -> RTSP stream
add_executable(dual_v4l2_rga_blend_enc_rtsp
    dual_v4l2_rga_blend_enc_rtsp.cpp
)

target_include_directories(dual_v4l2_rga_blend_enc_rtsp PRIVATE
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/v4l2
    ${CMAKE_SOURCE_DIR}/src/decoder
    ${CMAKE_SOURCE_DIR}/src/encoder
    ${CMAKE_SOURCE_DIR}/src/rtsp
    ${CMAKE_SOURCE_DIR}/src/rga
)

target_link_libraries(dual_v4l2_rga_blend_enc_rtsp PRIVATE
    medtor_v4l2
    medtor_decoder
    medtor_encoder
    medtor_rtsp
    medtor_rga
    medtor_common
    ${FFMPEG_LIB_DIR}/libavformat.a
    ${FFMPEG_LIB_DIR}/libavdevice.a
    ${FFMPEG_LIB_DIR}/libavfilter.a
    ${FFMPEG_LIB_DIR}/libavcodec.a
    ${FFMPEG_LIB_DIR}/libswscale.a
    ${FFMPEG_LIB_DIR}/libswresample.a
    ${FFMPEG_LIB_DIR}/libpostproc.a
    ${FFMPEG_LIB_DIR}/libavutil.a
    rockchip_mpp
    drm
    rga
    m
    pthread
    dl
    z
    bz2
)

# RKNN YOLO11 object detection demo
add_executable(rknn_yolo11
    rknn_yolo11.cpp
)

target_include_directories(rknn_yolo11 PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rknn
    ${CMAKE_SOURCE_DIR}/src/include/rknpu
)

target_link_libraries(rknn_yolo11 PRIVATE
    medtor_rknn
    ${CMAKE_SOURCE_DIR}/lib/rknpu/librknnrt.so
    rga
    m
    pthread
    dl
)

# YOLO11 HTTP server (web-based object detection)
add_executable(yolo11_http_server
    yolo11_http_server.cpp
)

target_include_directories(yolo11_http_server PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rknn
    ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(yolo11_http_server PRIVATE
    medtor_rknn
    ${CMAKE_SOURCE_DIR}/lib/rknpu/librknnrt.so
    ${CMAKE_SOURCE_DIR}/lib/workflow/libworkflow.a
    rga
    ssl
    crypto
    m
    pthread
    dl
    z
    bz2
)