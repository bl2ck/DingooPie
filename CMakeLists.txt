cmake_minimum_required(VERSION 3.16)

project(DingooPie LANGUAGES C CXX)

set(targetName DingooPie)
set(DINGOO_PIE_DEPS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/deps_extract" CACHE PATH "Extracted third-party dependency root")
set(PPSSPP_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ppsspp-master" CACHE PATH "PPSSPP source tree used for the IR JIT backend")
set(DYNARMIC_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dynarmic" CACHE PATH "Dynarmic source tree used for the ARM32 JIT backend")
set(DYNARMIC_BOOST_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps_extract/boost/lib/native/include" CACHE PATH "Boost include directory used by Dynarmic")
option(DINGOO_PIE_ENABLE_PPSSPP_IRJIT "Compile the PPSSPP IR/x64 JIT backend adapter" ON)
option(DINGOO_PIE_ENABLE_ARM32_DYNARMIC "Compile the Dynarmic ARM32 JIT backend" ON)
option(DINGOO_PIE_CONSOLE_SUBSYSTEM "Build DingooPie as a console executable for log-capturing validation runs" OFF)

set(DINGOO_PIE_SOURCE_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/app/cpu"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/app/hle"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/app/memory"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/app/runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/app/save"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/config/cheats"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/config/compatibility"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/config/settings"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/frontend/audio"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/frontend/input"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/frontend/video"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/shared"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/shared/diagnostics"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/shared/execution"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/shared/game"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/shared/services"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/pc/debug"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/pc/frontend"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/pc/platform"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/pc/resources"
)

set(DINGOO_PIE_ARM32_DYNARMIC_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/cc/cpu/arm32_dynarmic.cpp")

file(GLOB_RECURSE targetName_src CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/native/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/native/pc/*.cpp"
)

list(REMOVE_ITEM targetName_src ${DINGOO_PIE_ARM32_DYNARMIC_SOURCE})

if(WIN32)
    list(APPEND targetName_src
        "${CMAKE_CURRENT_SOURCE_DIR}/native/pc/resources/app_icon.rc"
    )
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/native/pc/resources/app_icon.rc" PROPERTIES
        OBJECT_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/native/pc/resources/app_metadata.h;${CMAKE_CURRENT_SOURCE_DIR}/native/pc/resources/resource_ids.h;${CMAKE_CURRENT_SOURCE_DIR}/native/pc/resources/app.manifest;${CMAKE_CURRENT_SOURCE_DIR}/resources/app_icon.ico"
    )
endif()

list(APPEND targetName_src
    "${PPSSPP_SOURCE_ROOT}/Common/Crypto/sha256.cpp"
)

if(DINGOO_PIE_ENABLE_PPSSPP_IRJIT)
    set(dingoopie_ppsspp_adapter_src
        "${CMAKE_CURRENT_SOURCE_DIR}/native/core/app/cpu/ppsspp_irjit_backend.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/native/core/app/cpu/ppsspp_shim.cpp"
    )
    list(REMOVE_ITEM targetName_src ${dingoopie_ppsspp_adapter_src})

    set(ppsspp_irjit_src
        "${PPSSPP_SOURCE_ROOT}/ext/xxhash.c"
        "${PPSSPP_SOURCE_ROOT}/Common/ABI.cpp"
        "${PPSSPP_SOURCE_ROOT}/Common/Buffer.cpp"
        "${PPSSPP_SOURCE_ROOT}/Common/File/Path.cpp"
        "${PPSSPP_SOURCE_ROOT}/Common/MemoryUtil.cpp"
        "${PPSSPP_SOURCE_ROOT}/Common/StringUtils.cpp"
        "${PPSSPP_SOURCE_ROOT}/Common/x64Emitter.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/MIPSCodeUtils.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/MIPSDis.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/MIPSDisVFPU.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/MIPSInt.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/MIPSIntVFPU.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/MIPSTables.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/MIPSVFPUFallbacks.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/MIPSVFPUUtils.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/JitCommon/JitBlockCache.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/JitCommon/JitState.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/IR/IRAnalysis.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/IR/IRCompALU.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/IR/IRCompBranch.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/IR/IRCompFPU.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/IR/IRCompLoadStore.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/IR/IRCompVFPU.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/IR/IRFrontend.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/IR/IRInst.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/IR/IRInterpreter.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/IR/IRJit.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/IR/IRNativeCommon.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/IR/IRPassSimplify.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/IR/IRRegCache.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/x86/X64IRAsm.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/x86/X64IRCompALU.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/x86/X64IRCompBranch.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/x86/X64IRCompFPU.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/x86/X64IRCompLoadStore.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/x86/X64IRCompSystem.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/x86/X64IRCompVec.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/x86/X64IRJit.cpp"
        "${PPSSPP_SOURCE_ROOT}/Core/MIPS/x86/X64IRRegCache.cpp"
    )

    add_library(ppsspp_irjit_core STATIC ${ppsspp_irjit_src})
    set_target_properties(ppsspp_irjit_core PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS YES
    )
    target_include_directories(ppsspp_irjit_core PRIVATE
        ${DINGOO_PIE_SOURCE_INCLUDE_DIRS}
        "${PPSSPP_SOURCE_ROOT}"
        "${PPSSPP_SOURCE_ROOT}/Common"
        "${PPSSPP_SOURCE_ROOT}/Core"
    )
    target_compile_definitions(ppsspp_irjit_core PRIVATE DINGOO_PIE_DINGOO_MEMORY _WIN32_WINNT=0x0601)

    add_library(dingoopie_ppsspp_adapter STATIC ${dingoopie_ppsspp_adapter_src})
    set_target_properties(dingoopie_ppsspp_adapter PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS YES
    )
    target_compile_definitions(dingoopie_ppsspp_adapter PRIVATE DINGOO_PIE_ENABLE_PPSSPP_IRJIT DINGOO_PIE_DINGOO_MEMORY _WIN32_WINNT=0x0601)
    target_include_directories(dingoopie_ppsspp_adapter PRIVATE
        ${DINGOO_PIE_SOURCE_INCLUDE_DIRS}
        "${PPSSPP_SOURCE_ROOT}"
        "${PPSSPP_SOURCE_ROOT}/Common"
        "${PPSSPP_SOURCE_ROOT}/Core"
    )
    target_link_libraries(dingoopie_ppsspp_adapter PUBLIC ppsspp_irjit_core)
endif()

add_executable(${targetName} ${targetName_src})
set_target_properties(${targetName} PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS YES
)

function(dingoopie_configure_arm32_dynarmic application_target)
    if(NOT DINGOO_PIE_ENABLE_ARM32_DYNARMIC)
        message(STATUS "DingooPie ARM32 backend: interpreter (Dynarmic disabled)")
        return()
    endif()

    set(missing_dependencies)
    if(NOT EXISTS "${DYNARMIC_SOURCE_ROOT}/CMakeLists.txt")
        list(APPEND missing_dependencies "Dynarmic source: ${DYNARMIC_SOURCE_ROOT}")
    endif()
    if(NOT EXISTS "${DYNARMIC_BOOST_INCLUDE_DIR}/boost/version.hpp")
        list(APPEND missing_dependencies "Boost headers: ${DYNARMIC_BOOST_INCLUDE_DIR}")
    endif()
    if(missing_dependencies)
        string(JOIN "; " missing_dependency_summary ${missing_dependencies})
        message(WARNING
            "DingooPie ARM32 backend: interpreter. Dynarmic was requested but dependencies are missing: "
            "${missing_dependency_summary}. Run scripts/bootstrap_windows.ps1 to install them.")
        return()
    endif()

    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    set(DYNARMIC_USE_BUNDLED_EXTERNALS ON CACHE BOOL "" FORCE)
    set(DYNARMIC_TESTS OFF CACHE BOOL "" FORCE)
    set(DYNARMIC_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
    set(Boost_INCLUDE_DIR "${DYNARMIC_BOOST_INCLUDE_DIR}" CACHE PATH "" FORCE)
    add_subdirectory("${DYNARMIC_SOURCE_ROOT}" "${CMAKE_BINARY_DIR}/dynarmic" EXCLUDE_FROM_ALL)
    add_library(dingoopie_dynarmic_adapter STATIC
        ${DINGOO_PIE_ARM32_DYNARMIC_SOURCE})
    set_target_properties(dingoopie_dynarmic_adapter PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS YES)
    target_include_directories(dingoopie_dynarmic_adapter PRIVATE
        ${DINGOO_PIE_SOURCE_INCLUDE_DIRS}
        "${DYNARMIC_SOURCE_ROOT}/src")
    target_link_libraries(dingoopie_dynarmic_adapter PUBLIC dynarmic)
    target_compile_definitions(${application_target} PRIVATE DINGOO_PIE_ARM32_DYNARMIC)
    target_link_libraries(${application_target} PRIVATE dingoopie_dynarmic_adapter)
    message(STATUS "DingooPie ARM32 backend: Dynarmic")
endfunction()

dingoopie_configure_arm32_dynarmic(${targetName})

target_compile_definitions(${targetName} PRIVATE _WIN32_WINNT=0x0601 SDL_MAIN_HANDLED)

target_include_directories(${targetName} PRIVATE
    ${DINGOO_PIE_SOURCE_INCLUDE_DIRS}
    "${DINGOO_PIE_DEPS_ROOT}/capstone/mingw64/include"
    "${DINGOO_PIE_DEPS_ROOT}/SDL2/SDL2-2.26.5/x86_64-w64-mingw32/include"
    "${DINGOO_PIE_DEPS_ROOT}/SDL2/SDL2-2.26.5/x86_64-w64-mingw32/include/SDL2"
)

if(DINGOO_PIE_ENABLE_PPSSPP_IRJIT)
    target_compile_definitions(${targetName} PRIVATE DINGOO_PIE_ENABLE_PPSSPP_IRJIT)
    target_include_directories(${targetName} PRIVATE
        "${PPSSPP_SOURCE_ROOT}"
        "${PPSSPP_SOURCE_ROOT}/Common"
        "${PPSSPP_SOURCE_ROOT}/Core"
    )
    target_link_libraries(${targetName} PRIVATE dingoopie_ppsspp_adapter)
endif()

target_link_libraries(${targetName} PRIVATE
    "${DINGOO_PIE_DEPS_ROOT}/capstone/mingw64/lib/libcapstone.dll.a"
    "${DINGOO_PIE_DEPS_ROOT}/SDL2/SDL2-2.26.5/x86_64-w64-mingw32/lib/libSDL2.dll.a"
    imm32
    gdiplus
    comctl32
    uxtheme
    shell32
    winmm
    pthread
)

get_filename_component(DINGOO_PIE_COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
set(DINGOO_PIE_WINPTHREAD_DLL "${DINGOO_PIE_COMPILER_BIN_DIR}/libwinpthread-1.dll")
if(NOT EXISTS "${DINGOO_PIE_WINPTHREAD_DLL}")
    set(DINGOO_PIE_WINPTHREAD_DLL "${DINGOO_PIE_DEPS_ROOT}/winpthread/mingw64/bin/libwinpthread-1.dll")
endif()
set(DINGOO_PIE_RUNTIME_DLLS
    "${DINGOO_PIE_DEPS_ROOT}/SDL2/SDL2-2.26.5/x86_64-w64-mingw32/bin/SDL2.dll"
    "${DINGOO_PIE_DEPS_ROOT}/capstone/mingw64/bin/libcapstone.dll"
    "${DINGOO_PIE_WINPTHREAD_DLL}"
)

foreach(runtimeDll IN LISTS DINGOO_PIE_RUNTIME_DLLS)
    if(EXISTS "${runtimeDll}")
        add_custom_command(TARGET ${targetName} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${runtimeDll}"
                "$<TARGET_FILE_DIR:${targetName}>"
        )
    else()
        message(WARNING "Runtime DLL not found: ${runtimeDll}")
    endif()
endforeach()

if(MINGW)
    if(DINGOO_PIE_CONSOLE_SUBSYSTEM)
        target_link_options(${targetName} PRIVATE -mconsole -static-libgcc -static-libstdc++)
    else()
        target_link_options(${targetName} PRIVATE -mwindows -static-libgcc -static-libstdc++)
    endif()
endif()
