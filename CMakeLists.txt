cmake_minimum_required(VERSION 3.21)

project(IMETip VERSION 0.1.1 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "IMETip 仅支持 Windows 平台（Win32 / GDI+ / UI Automation）")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(IMETIP_BUILD_TESTS "构建单元测试" ON)
option(IMETIP_BUILD_TOOLS "构建辅助工具（图标 / 预览 / 验证宿主）" ON)

set(IMETIP_SOURCES
    src/main.cpp
    src/app/App.cpp
    src/app/Config.cpp
    src/core/EventMonitor.cpp
    src/core/ImeState.cpp
    src/core/InputProbe.cpp
    src/platform/Autostart.cpp
    src/platform/Dpi.cpp
    src/platform/UiaProbe.cpp
    src/ui/AppIcon.cpp
    src/ui/IndicatorRenderer.cpp
    src/ui/IndicatorWindow.cpp
    src/ui/TrayIcon.cpp
    src/util/Log.cpp
)

add_executable(IMETip WIN32 ${IMETIP_SOURCES} res/IMETip.rc)

target_include_directories(IMETip PRIVATE src)
target_compile_definitions(IMETip PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)
target_link_libraries(IMETip PRIVATE
    user32
    gdi32
    gdiplus
    ole32
    oleaut32
    shcore
    shell32
    advapi32
    imm32
)

if(MSVC)
    target_compile_options(IMETip PRIVATE /W4 /permissive- /utf-8 /EHsc /Zc:__cplusplus)
    # 清单已由 res/IMETip.rc 以 RT_MANIFEST 嵌入，关闭链接器自动生成，避免资源重复
    target_link_options(IMETip PRIVATE /OPT:REF /OPT:ICF /MANIFEST:NO)
    # 静态链接 CRT：单文件 exe，不依赖 VC++ 运行库
    set_property(TARGET IMETip PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    set_property(TARGET IMETip PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

if(IMETIP_BUILD_TOOLS)
    add_executable(imetip-icon tools/icon_gen.cpp src/ui/AppIcon.cpp)

    add_executable(imetip-preview tools/preview_gen.cpp src/ui/IndicatorRenderer.cpp)

    add_executable(imetip-smoke-host WIN32 tools/smoke_host.cpp)

    foreach(tool_target imetip-icon imetip-preview imetip-smoke-host)
        target_include_directories(${tool_target} PRIVATE src)
        target_compile_definitions(${tool_target} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_link_libraries(${tool_target} PRIVATE user32 gdi32 gdiplus ole32)
        if(MSVC)
            target_compile_options(${tool_target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
            set_property(TARGET ${tool_target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        endif()
    endforeach()
endif()

if(IMETIP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# 便捷目标：重新生成 res/IMETip.ico
if(TARGET imetip-icon)
    add_custom_target(icons
        COMMAND imetip-icon "${CMAKE_SOURCE_DIR}/res/IMETip.ico"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "重新生成 res/IMETip.ico"
        DEPENDS imetip-icon
    )
endif()
