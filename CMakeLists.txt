cmake_minimum_required(VERSION 3.20)
project(grid_project CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # Para que Zed y clangd reconozcan las dependencias

# 1. Buscar las librerías del sistema
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

# 2. DECLARAR EL EJECUTABLE PRIMERO (Esto soluciona el error)
# Cambia 'main.cpp' por los archivos de código que uses
add_executable(grid main.cpp)

# 3. Incluir las carpetas de cabeceras (.h)
target_include_directories(grid PRIVATE
    ${GLEW_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include # Tu carpeta de dependencias local si tienes una
)

# 4. ENLAZAR LAS LIBRERÍAS AL OBJETIVO YA CREADO
target_link_libraries(grid PRIVATE
    GLEW::GLEW
    OpenGL::GL
)
