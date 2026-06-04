# Decoder module: RTSP puller + RKMPP decoder + NV12 file writer
add_library(medtor_decoder STATIC
    rtsp_puller.cpp
    rkmpp_decoder.cpp
    nv12_file_writer_node.cpp
    mjpeg_file_reader_node.cpp
)

target_include_directories(medtor_decoder PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
    ${FFMPEG_INCLUDE_DIR}
    ${TOOLCHAIN_SYSROOT}/usr/include/libdrm
)

target_link_libraries(medtor_decoder PUBLIC medtor_common)
