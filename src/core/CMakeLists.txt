add_library(ter_core
    core.cpp
)

target_include_directories(ter_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)
target_link_libraries(ter_core PUBLIC Threads::Threads)
