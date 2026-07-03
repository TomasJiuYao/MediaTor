set(LIBRGA_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/../../librga/include)

add_library(medtor_rga STATIC
    rga_processor.cpp
)

target_include_directories(medtor_rga PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
    ${LIBRGA_INCLUDE_DIR}
)

target_link_libraries(medtor_rga PUBLIC medtor_common rga)