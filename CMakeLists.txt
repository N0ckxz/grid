cmake_minimum_required(VERSION 3.16)
project(grid CXX C) # Se añade C porque glad.c es código C puro

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Buscar los paquetes de OpenGL y GLFW
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

# 2. Agregar la carpeta 'include/' para que encuentre <glad/glad.h>
include_directories(include)

# 3. Incluir main.cpp y src/glad.c
add_executable(grid 
    main.cpp
    src/glad.c
)

# 4. Enlazar las librerías necesarias
target_link_libraries(grid PRIVATE
    OpenGL::GL
    glfw
    ${CMAKE_DL_LIBS}
)