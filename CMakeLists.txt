cmake_minimum_required(VERSION 3.16)
project(glitcher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW3 REQUIRED glfw3)
pkg_check_modules(GL REQUIRED gl)

add_executable(glitcher
    src/main.cpp
    deps/imgui/imgui.cpp
    deps/imgui/imgui_demo.cpp
    deps/imgui/imgui_draw.cpp
    deps/imgui/imgui_tables.cpp
    deps/imgui/imgui_widgets.cpp
    deps/imgui/backends/imgui_impl_glfw.cpp
    deps/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(glitcher PRIVATE
    deps/imgui
    deps/imgui/backends
    deps
    ${GLFW3_INCLUDE_DIRS}
    ${GL_INCLUDE_DIRS}
)

target_link_libraries(glitcher PRIVATE
    ${GLFW3_LIBRARIES}
    ${GL_LIBRARIES}
    dl
)

target_compile_options(glitcher PRIVATE ${GLFW3_CFLAGS_OTHER})
