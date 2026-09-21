cmake_minimum_required(VERSION 3.16)
project(grid CXX C) 

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui")
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(grid 
    main.cpp
    src/glad.c
    ${IMGUI_SOURCES}
)

target_include_directories(grid PRIVATE
    include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(grid PRIVATE
    OpenGL::GL
    glfw
    ${CMAKE_DL_LIBS}
)
