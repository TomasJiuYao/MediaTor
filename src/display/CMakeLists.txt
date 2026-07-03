# DRM/KMS display module
add_library(medtor_display STATIC
    drm_display.cpp
)

target_include_directories(medtor_display PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
    ${FFMPEG_INCLUDE_DIR}
    ${TOOLCHAIN_SYSROOT}/usr/include/libdrm
)

target_link_libraries(medtor_display PUBLIC
    medtor_common
    drm
    ${FFMPEG_LIB_DIR}/libswscale.a
    ${FFMPEG_LIB_DIR}/libavutil.a
)