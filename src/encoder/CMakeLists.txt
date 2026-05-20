# Encoder module (FFmpeg/RKMPP encoder + file writer node)
add_library(medtor_encoder STATIC
    ffmpeg_encoder.cpp
    ffmpeg_encoder_node.cpp
    file_writer_node.cpp
)

target_include_directories(medtor_encoder PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
    ${FFMPEG_INCLUDE_DIR}
)

target_link_libraries(medtor_encoder PUBLIC medtor_common)
