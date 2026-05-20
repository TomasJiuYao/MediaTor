# V4L2 capture module
add_library(medtor_v4l2 STATIC
    v4l2_capture.cpp
    v4l2_capture_node.cpp
)

target_include_directories(medtor_v4l2 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
)

target_link_libraries(medtor_v4l2 PUBLIC medtor_common)
