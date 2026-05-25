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
#   avformat → avcodec → swresample → avutil
#   avcodec  → swresample, avutil
#   swscale  → avutil
target_link_libraries(v4l2_rkmpp_enc PRIVATE
    medtor_v4l2
    medtor_encoder
    medtor_rtsp
    medtor_common
    ${FFMPEG_LIB_DIR}/libavformat.a
    ${FFMPEG_LIB_DIR}/libavcodec.a
    ${FFMPEG_LIB_DIR}/libswresample.a
    ${FFMPEG_LIB_DIR}/libavutil.a
    ${FFMPEG_LIB_DIR}/libswscale.a
    ${FFMPEG_LIB_DIR}/libavdevice.a
    ${FFMPEG_LIB_DIR}/libavfilter.a
    ${FFMPEG_LIB_DIR}/libpostproc.a
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
    ${FFMPEG_LIB_DIR}/libavcodec.a
    ${FFMPEG_LIB_DIR}/libswresample.a
    ${FFMPEG_LIB_DIR}/libavutil.a
    ${FFMPEG_LIB_DIR}/libswscale.a
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
    ${FFMPEG_LIB_DIR}/libavcodec.a
    ${FFMPEG_LIB_DIR}/libswresample.a
    ${FFMPEG_LIB_DIR}/libavutil.a
    ${FFMPEG_LIB_DIR}/libswscale.a
    rockchip_mpp
    drm
    rga
    m
    pthread
    dl
    z
    bz2
)
