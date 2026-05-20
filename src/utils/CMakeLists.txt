add_library(ter_utils
    utils.cpp
)

target_include_directories(ter_utils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ter_utils PUBLIC ter_core)
