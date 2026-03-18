cmake_minimum_required(VERSION 3.20)
project(Amhook LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/Amhook.cpp
    deps/hde64.cpp
    include/imgui/imgui.cpp
    include/imgui/imgui_demo.cpp
    include/imgui/imgui_tables.cpp
    include/imgui/imgui_widgets.cpp
    include/imgui/imgui_impl_win32.cpp
    include/imgui/imgui_impl_dx11.cpp
)

set(HEADERS
    include/Amhk.h
    deps/hde64.h
    deps/table64.h
    include/imgui/imgui.h
    include/imgui/imgui_impl_win32.h
    include/imgui/imgui_impl_dx11.h
)

include_directories(include deps)

add_library(Amhook SHARED src/Amhook.cpp deps/hde64.cpp)
if(MSVC)
    target_compile_definitions(Amhook PRIVATE Amhook_EXPORTS)
endif()
target_link_libraries(Amhook PRIVATE d3d11 dxgi)

add_executable(AmhookTest src/main.cpp)
target_link_libraries(AmhookTest PRIVATE Amhook d3d11 dxgi d3dcompiler)
if(MSVC)
    target_link_options(AmhookTest PRIVATE /SUBSYSTEM:WINDOWS /NODEFAULTLIB:MSVCRT)
endif()