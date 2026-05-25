# Decoder module: RTSP puller + RKMPP decoder
add_library(medtor_decoder STATIC
    rtsp_puller.cpp
    rkmpp_decoder.cpp
)

target_include_directories(medtor_decoder PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
    ${FFMPEG_INCLUDE_DIR}
    ${TOOLCHAIN_SYSROOT}/usr/include/libdrm
)

target_link_libraries(medtor_decoder PUBLIC medtor_common)
