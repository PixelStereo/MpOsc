cmake_minimum_required(VERSION 3.22)
include(cmake/Version.cmake)
project(MpOsc VERSION ${MPOSC_VERSION_NUMERIC} LANGUAGES CXX)
configure_file(cmake/Version.h.in generated/MpOscVersion.h @ONLY)
file(WRITE "${CMAKE_BINARY_DIR}/application.json"
     "{\"name\":\"${MPOSC_APP_NAME}\",\"version\":\"${MPOSC_VERSION}\"}\n")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Multimedia)
# Shared Pixel Stereo code (the OSC codec), pinned as a submodule
set(PXS_BUILD_TESTS OFF)
add_subdirectory(third_party/pxs-qt EXCLUDE_FROM_ALL)
qt_add_executable(MpOsc WIN32 MACOSX_BUNDLE
    src/app/main.cpp src/app/SelfTest.cpp src/app/SelfTest.h
    src/engine/Destinations.cpp src/engine/MediaPipeClient.cpp src/engine/MediaPipeClient.h
    src/engine/MediaPipeRuntime.cpp src/engine/SignalFilters.cpp src/engine/SignalFilters.h src/engine/Signals.cpp src/engine/VideoInput.cpp src/engine/VideoInput.h
    src/ui/About.cpp src/ui/About.h src/ui/HoverHints.cpp src/ui/HoverHints.h src/ui/SignalPanel.cpp src/ui/SignalPanel.h src/ui/SignalWidgets.h
    src/ui/Theme.cpp src/ui/TrackingOverlay.cpp
    src/ui/VideoPreview.h)
# Sources include each other by file name: engine (no widgets), ui (widgets), app (entry point and self-test)
target_include_directories(MpOsc PRIVATE src/app src/engine src/ui)
qt_add_resources(MpOsc "branding" PREFIX "/" FILES assets/MpOsc.png)
if(APPLE)
    target_sources(MpOsc PRIVATE assets/MpOsc.icns)
    set_source_files_properties(assets/MpOsc.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
elseif(WIN32)
    target_sources(MpOsc PRIVATE assets/windows.rc)
    set_property(SOURCE assets/windows.rc APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/MpOsc.ico")
    target_include_directories(MpOsc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()
set_target_properties(MpOsc PROPERTIES AUTOMOC ON)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS scripts/mediapipe_worker.py)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mediapipe_worker.py" WORKER)
configure_file(src/engine/Worker.h.in generated/Worker.h @ONLY)
target_include_directories(MpOsc PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(MpOsc PRIVATE Qt6::Widgets Qt6::Network Qt6::Multimedia pxs::osc pxs::theme pxs::widgets pxs::tree pxs::bind pxs::oscquery pxs::interaction pxs::filterview)
set_target_properties(MpOsc PROPERTIES OUTPUT_NAME "${MPOSC_APP_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER com.pixelstereo.mp-osc
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in")

if(APPLE)
    enable_language(C OBJC OBJCXX)
    set_target_properties(MpOsc PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED ON)
    set(syphon_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/syphon")
    if(NOT EXISTS "${syphon_dir}/SyphonMetalClient.h")
        message(FATAL_ERROR "Syphon is missing. Run git submodule update --init.")
    endif()
    file(GLOB syphon_headers "${syphon_dir}/*.h")
    file(COPY ${syphon_headers} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/syphon-include/Syphon")
    file(GLOB syphon_sources "${syphon_dir}/*.m" "${syphon_dir}/*.c")
    list(FILTER syphon_sources EXCLUDE REGEX "(SyphonMetalServer|SyphonServerRendererMetal)[.]m$")
    add_library(mposc_syphon STATIC ${syphon_sources})
    target_include_directories(mposc_syphon PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/syphon-include"
                               PRIVATE "${syphon_dir}")
    target_compile_definitions(mposc_syphon PRIVATE SYPHON_CORE_SHARE GL_SILENCE_DEPRECATION)
    target_compile_options(mposc_syphon PRIVATE "SHELL:-include '${syphon_dir}/Syphon_Prefix.pch'"
                           $<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>)
    target_link_libraries(mposc_syphon PUBLIC "-framework Cocoa" "-framework OpenGL"
                          "-framework IOSurface" "-framework Metal")
    target_sources(MpOsc PRIVATE src/engine/SyphonInput.mm)
    set_source_files_properties(src/engine/SyphonInput.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(MpOsc PRIVATE mposc_syphon)
elseif(WIN32)
    set(spout_sdk "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spout2/SPOUTSDK")
    set(spout_dx "${spout_sdk}/SpoutDirectX/SpoutDX")
    set(spout_gl "${spout_sdk}/SpoutGL")
    if(NOT EXISTS "${spout_dx}/SpoutDX.h")
        message(FATAL_ERROR "Spout2 is missing. Run git submodule update --init.")
    endif()
    add_library(mposc_spout STATIC
        "${spout_dx}/SpoutDX.cpp" "${spout_gl}/SpoutCopy.cpp" "${spout_gl}/SpoutDirectX.cpp"
        "${spout_gl}/SpoutFrameCount.cpp" "${spout_gl}/SpoutSenderNames.cpp"
        "${spout_gl}/SpoutSharedMemory.cpp" "${spout_gl}/SpoutUtils.cpp")
    target_include_directories(mposc_spout PUBLIC "${spout_dx}" "${spout_gl}")
    target_compile_definitions(mposc_spout PUBLIC SPOUT_BUILD_STATIC NOMINMAX)
    target_link_libraries(mposc_spout PUBLIC opengl32 d3d9 d3d11 dxgi version winmm shlwapi)
    target_sources(MpOsc PRIVATE src/engine/SpoutInput.cpp)
    target_link_libraries(MpOsc PRIVATE mposc_spout)
endif()

# The signal panel and its filters, in a window of their own (no camera, no MediaPipe)
option(MPOSC_TESTS "Build the interface tests" ON)
if(MPOSC_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(mposc_ui_test tests/ui_test.cpp
        src/engine/Destinations.cpp src/engine/SignalFilters.cpp src/engine/Signals.cpp
        src/ui/HoverHints.cpp src/ui/SignalPanel.cpp src/ui/Theme.cpp)
    set_target_properties(mposc_ui_test PROPERTIES AUTOMOC ON)
    target_include_directories(mposc_ui_test PRIVATE src/engine src/ui)
    target_link_libraries(mposc_ui_test PRIVATE Qt6::Widgets Qt6::Network Qt6::Test pxs::osc pxs::theme pxs::widgets pxs::interaction pxs::filterview)
    add_test(NAME ui COMMAND mposc_ui_test)
    set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
