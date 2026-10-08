cmake_minimum_required(VERSION 3.16)
project(oscill_lcd C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(SCOPE_BUILD_PREVIEW "Build the desktop/SDL frontend" ON)
option(SCOPE_ENABLE_DRM "Enable direct DRM dumb-buffer output and evdev touch" OFF)
option(SCOPE_ENABLE_FRAMEBUFFER "Enable Linux framebuffer output and evdev touch" OFF)
option(SCOPE_ENABLE_ZYNQ "Enable the Linux Zynq capture adapter" OFF)
option(SCOPE_BUILD_BENCHMARKS "Build synthetic capture and renderer benchmarks" ON)
find_package(Threads REQUIRED)

add_library(control_protocol STATIC src/control_protocol.c)
target_include_directories(control_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_library(scope_runtime STATIC
    src/scope_thread.c src/capture.c src/capture_processor.c src/capture_file.c
    src/capture_pipeline.c src/control_transport.c src/control_serial.c)
target_include_directories(scope_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(scope_runtime PUBLIC control_protocol Threads::Threads)
if(WIN32)
    target_compile_definitions(scope_runtime PUBLIC _WIN32_WINNT=0x0602)
endif()
if(NOT MSVC)
    target_link_libraries(scope_runtime PUBLIC m)
endif()
if(SCOPE_ENABLE_ZYNQ)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "The Zynq capture adapter requires Linux")
    endif()
    target_sources(scope_runtime PRIVATE src/capture_zynq.c)
    target_compile_definitions(scope_runtime PUBLIC SCOPE_ENABLE_ZYNQ=1)
endif()

add_library(scope_screen STATIC src/scope_screen.c)
target_include_directories(scope_screen PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

include(CTest)
if(BUILD_TESTING)
    add_executable(scope_sim_test
        src/tests/test_demo_signal.c
        src/demo_signal.c
        src/wave_file.c
    )
    target_link_libraries(scope_sim_test PRIVATE scope_screen)
    if(NOT MSVC)
        target_link_libraries(scope_sim_test PRIVATE m)
    endif()
    add_test(NAME scope_simulator COMMAND scope_sim_test)
    add_executable(scope_runtime_test src/tests/test_runtime.c)
    target_link_libraries(scope_runtime_test PRIVATE scope_runtime)
    add_test(NAME scope_runtime COMMAND scope_runtime_test)
endif()

if(SCOPE_BUILD_BENCHMARKS)
    add_executable(scope_benchmark src/tests/benchmark.c src/demo_signal.c)
    target_link_libraries(scope_benchmark PRIVATE scope_runtime scope_screen)
endif()

if(SCOPE_BUILD_PREVIEW AND WIN32)
    add_executable(scope_preview WIN32
        src/preview_win32.c
        src/panel_win32.c
        src/demo_signal.c
        src/wave_file.c
    )
    if(MINGW)
        target_compile_definitions(scope_preview PRIVATE _WIN32_WINNT=0x0602)
        target_link_options(scope_preview PRIVATE -municode)
        target_link_libraries(scope_preview PRIVATE m)
    endif()
    target_link_libraries(scope_preview PRIVATE scope_screen user32 gdi32 shell32)
elseif(SCOPE_BUILD_PREVIEW AND UNIX AND NOT APPLE)
    find_package(SDL2 CONFIG QUIET)
    if(TARGET SDL2::SDL2)
        set(SCOPE_SDL_TARGET SDL2::SDL2)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
        set(SCOPE_SDL_TARGET PkgConfig::SDL2)
    endif()
    add_executable(scope_preview
        src/preview_sdl.c
        src/panel_sdl.c
        src/demo_signal.c
        src/wave_file.c
        src/capture_adapter.c
    )
    target_link_libraries(scope_preview PRIVATE scope_screen scope_runtime ${SCOPE_SDL_TARGET} m)
    if(SCOPE_ENABLE_DRM OR SCOPE_ENABLE_FRAMEBUFFER)
        target_sources(scope_preview PRIVATE src/linux_display.c src/linux_touch.c)
        target_compile_definitions(scope_preview PRIVATE SCOPE_ENABLE_LINUX_DISPLAY=1)
    endif()
    if(SCOPE_ENABLE_DRM)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(DRM REQUIRED IMPORTED_TARGET libdrm)
        target_compile_definitions(scope_preview PRIVATE SCOPE_ENABLE_DRM=1)
        target_link_libraries(scope_preview PRIVATE PkgConfig::DRM)
    endif()
endif()

if(TARGET scope_preview)
    install(TARGETS scope_preview RUNTIME DESTINATION bin)
endif()
