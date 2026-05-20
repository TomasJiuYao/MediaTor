# RTSP streaming module
add_library(medtor_rtsp STATIC
    rtsp_streamer.cpp
    rtsp_streamer_node.cpp
)

target_include_directories(medtor_rtsp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
    ${FFMPEG_INCLUDE_DIR}
)

target_link_libraries(medtor_rtsp PUBLIC medtor_common)
