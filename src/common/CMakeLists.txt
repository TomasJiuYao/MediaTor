# Common infrastructure: node base, pipeline, queue, epoll, frame/packet types
add_library(medtor_common STATIC
    pipeline.cpp
)

target_include_directories(medtor_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIR}
)

find_package(Threads REQUIRED)
target_link_libraries(medtor_common PUBLIC Threads::Threads)
