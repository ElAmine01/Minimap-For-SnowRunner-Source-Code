cmake_minimum_required(VERSION 3.20)
project(SnowMapMod LANGUAGES C CXX)

# ---------------------------------------------------------------------------
#  SnowMap — SnowRunner minimap mod
#  Builds two artifacts:
#    * dinput8.dll  (proxy that loads SnowMap.asi)
#    * SnowMap.asi  (actual mod, a renamed .dll)
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 64-bit only — SnowRunner is x64
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "SnowMap requires a 64-bit toolchain (SnowRunner is x64).")
endif()

# MSVC & MinGW flags
if(MSVC)
    add_compile_options(/W3 /permissive- /Zc:__cplusplus /MP /EHsc)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    # static CRT so the DLL has no extra runtime dep
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
endif()

# ---------------------------------------------------------------------------
#  Third-party locations.
#  These are expected under third_party/ — see README for fetch instructions.
# ---------------------------------------------------------------------------
set(TP "${CMAKE_SOURCE_DIR}/third_party")

# MinHook
set(MINHOOK_DIR     "${TP}/MinHook"     CACHE PATH "MinHook source")
# ImGui (docking branch)
set(IMGUI_DIR       "${TP}/imgui"       CACHE PATH "ImGui source")
# nlohmann/json (single header)
set(JSON_DIR        "${TP}/json"        CACHE PATH "nlohmann/json dir (with nlohmann/json.hpp)")
# DirectXTK (DDSTextureLoader + WICTextureLoader, DX11 variant)
set(DIRECTXTK_DIR   "${TP}/DirectXTK"   CACHE PATH "DirectXTK source")

# ---------------------------------------------------------------------------
#  MinHook — built as a static lib (tiny, ~5 files).
# ---------------------------------------------------------------------------
add_library(minhook STATIC
    "${MINHOOK_DIR}/src/buffer.c"
    "${MINHOOK_DIR}/src/hook.c"
    "${MINHOOK_DIR}/src/trampoline.c"
    "${MINHOOK_DIR}/src/hde/hde64.c"
)
target_include_directories(minhook PUBLIC "${MINHOOK_DIR}/include")
set_target_properties(minhook PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
#  ImGui — core + Win32 + DX11 backend.
# ---------------------------------------------------------------------------
add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
)
target_include_directories(imgui PUBLIC
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)

# ---------------------------------------------------------------------------
#  DirectXTK — only the two loaders we actually need.
#  (Building the full DirectXTK here is overkill.)
# ---------------------------------------------------------------------------
add_library(dxtk_loaders STATIC
    "${DIRECTXTK_DIR}/Src/DDSTextureLoader.cpp"
    "${DIRECTXTK_DIR}/Src/WICTextureLoader.cpp"
)
target_include_directories(dxtk_loaders PUBLIC
    "${DIRECTXTK_DIR}/Inc"
    "${DIRECTXTK_DIR}/Src"   # loaders pull LoaderHelpers.h / PlatformHelpers.h from here
)

# ---------------------------------------------------------------------------
#  Proxy DLL — dinput8.dll
# ---------------------------------------------------------------------------
add_library(snowmap_proxy SHARED
    src/proxy/dllmain.cpp
)
target_include_directories(snowmap_proxy PRIVATE src)
if(MSVC)
    # forward exports via .def file
    target_sources(snowmap_proxy PRIVATE src/proxy/dinput8.def)
else()
    # MinGW: use the .def directly as a source
    target_sources(snowmap_proxy PRIVATE src/proxy/dinput8.def)
endif()
set_target_properties(snowmap_proxy PROPERTIES
    OUTPUT_NAME "dinput8"
    PREFIX ""
    SUFFIX ".dll"
)
target_link_libraries(snowmap_proxy PRIVATE user32 shlwapi)

# ---------------------------------------------------------------------------
#  SnowMap.asi — the actual mod
# ---------------------------------------------------------------------------
file(GLOB_RECURSE ASI_SOURCES CONFIGURE_DEPENDS
    src/asi/*.cpp
    src/asi/*.h
)

add_library(snowmap_asi SHARED ${ASI_SOURCES})
target_include_directories(snowmap_asi PRIVATE
    src
    src/asi
    "${JSON_DIR}"
)
target_link_libraries(snowmap_asi PRIVATE
    minhook
    imgui
    dxtk_loaders
    d3d11
    dxgi
    dxguid
    xinput9_1_0
    user32
    gdi32
    advapi32
    shlwapi
    winhttp
    shell32
    ole32
    windowscodecs
)
set_target_properties(snowmap_asi PROPERTIES
    OUTPUT_NAME "SnowMap"
    PREFIX ""
    SUFFIX ".asi"   # SnowRunner loaders typically accept .asi; it's just a renamed DLL
)

# ---------------------------------------------------------------------------
#  Staging — drop both outputs + SnowMap/ runtime dir into build/dist
# ---------------------------------------------------------------------------
set(DIST_DIR "${CMAKE_BINARY_DIR}/dist")
add_custom_command(TARGET snowmap_asi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}/SnowMap"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:snowmap_proxy>" "${DIST_DIR}/dinput8.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:snowmap_asi>"   "${DIST_DIR}/SnowMap.asi"
    COMMENT "Staging SnowMap dist/"
)
