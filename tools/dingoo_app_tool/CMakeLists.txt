cmake_minimum_required(VERSION 3.16)

project(DingooAppTool VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DINGOO_APP_TOOL_CORE_SOURCES
    src/app_format.cpp
    src/app_format.h
    src/file_util.cpp
    src/file_util.h
    src/json_manifest.cpp
    src/json_manifest.h
    src/stx_editor.cpp
    src/stx_editor.h
)

add_executable(dingoo-app-tool
    ${DINGOO_APP_TOOL_CORE_SOURCES}
    src/main.cpp
)

if(WIN32)
    target_link_libraries(dingoo-app-tool PRIVATE shell32)
    add_executable(dingoo-app-tool-gui WIN32
        ${DINGOO_APP_TOOL_CORE_SOURCES}
        src/app_gui.cpp
    )
    target_link_libraries(dingoo-app-tool-gui PRIVATE shell32 comdlg32 comctl32 ole32)
endif()

set(DINGOO_APP_TOOL_TARGETS dingoo-app-tool)
if(WIN32)
    list(APPEND DINGOO_APP_TOOL_TARGETS dingoo-app-tool-gui)
endif()

foreach(target_name IN LISTS DINGOO_APP_TOOL_TARGETS)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -pedantic)
    endif()
endforeach()
