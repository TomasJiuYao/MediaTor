add_executable(v4l2_rkmpp_enc
    v4l2_rkmpp_enc.cpp
    v4l2_capture.cpp
    ffmpeg_encoder.cpp
    rtsp_streamer.cpp
    v4l2_capture_node.cpp
    ffmpeg_encoder_node.cpp
    rtsp_streamer_node.cpp
    file_writer_node.cpp
    pipeline.cpp
)

target_include_directories(v4l2_rkmpp_enc PRIVATE
    ${FFMPEG_INCLUDE_DIR}
)

target_link_libraries(v4l2_rkmpp_enc PRIVATE
    ${FFMPEG_LIB_DIR}/libavcodec.a
    ${FFMPEG_LIB_DIR}/libavformat.a
    ${FFMPEG_LIB_DIR}/libavdevice.a
    ${FFMPEG_LIB_DIR}/libavfilter.a
    ${FFMPEG_LIB_DIR}/libavutil.a
    ${FFMPEG_LIB_DIR}/libswresample.a
    ${FFMPEG_LIB_DIR}/libswscale.a
    ${FFMPEG_LIB_DIR}/libpostproc.a
    m
    pthread
    dl
    z
    bz2
    rockchip_mpp
    drm
    rga
)
