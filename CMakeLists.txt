cmake_minimum_required(VERSION 3.24)

project(video_looper CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# GLFW
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

# Dear ImGui
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG docking
)
FetchContent_MakeAvailable(imgui)

add_executable(video_looper MACOSX_BUNDLE
  app/src/main.cpp
  app/src/App.cpp
  app/src/gl/Shader.cpp
  app/src/gl/FullscreenQuad.cpp
  app/src/gl/FxPipeline.cpp
  app/src/buffer/RingBuffer.cpp
  app/src/looper/LooperEngine.cpp
  app/src/util/HotReload.cpp
  app/src/link/LinkBridge.cpp
  app/src/clock/Clock.hpp
  app/src/ui/ParamBus.cpp
  app/src/ui/ControlsPanel.cpp
  app/src/io/AVFWebcamSource.mm
  app/src/io/AVFFileSource.mm
  app/src/io/AVFRecorder.mm
  app/src/io/SourceController.cpp
  app/src/util/MacFileDialog.mm
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(video_looper PRIVATE app/src ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(video_looper PRIVATE SHADER_DIR="${CMAKE_SOURCE_DIR}/app/shaders")

target_link_libraries(video_looper PRIVATE glfw)

if(APPLE)
  # Provide Info.plist for bundle (required for camera privacy and Continuity Camera)
  set_target_properties(video_looper PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/app/macos/Info.plist"
  )
  # Silence deprecation warnings for OpenGL on macOS
  target_compile_definitions(video_looper PRIVATE GL_SILENCE_DEPRECATION)
  # Ensure we link the required macOS frameworks when building GLFW from source
  find_library(COCOA_FRAMEWORK Cocoa)
  find_library(APPKIT_FRAMEWORK AppKit)
  find_library(IOKIT_FRAMEWORK IOKit)
  find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
  find_library(COREVIDEO_FRAMEWORK CoreVideo)
  find_library(AVFOUNDATION_FRAMEWORK AVFoundation)
  find_library(COREMEDIA_FRAMEWORK CoreMedia)
  find_library(OPENGL_FRAMEWORK OpenGL)
  target_link_libraries(video_looper PRIVATE
    ${COCOA_FRAMEWORK}
    ${APPKIT_FRAMEWORK}
    ${IOKIT_FRAMEWORK}
    ${COREFOUNDATION_FRAMEWORK}
    ${COREVIDEO_FRAMEWORK}
    ${AVFOUNDATION_FRAMEWORK}
    ${COREMEDIA_FRAMEWORK}
    ${OPENGL_FRAMEWORK}
  )
endif()

# Optional Ableton Link
option(USE_ABLETON_LINK "Enable Ableton Link support" OFF)
set(ABLETON_LINK_INCLUDE_DIR "" CACHE PATH "Ableton Link include directory (path containing ableton/Link.hpp)")
set(ABLETON_LINK_LIBRARY "" CACHE FILEPATH "Ableton Link library file (.a or .dylib)")

if(USE_ABLETON_LINK)
  if(NOT ABLETON_LINK_ROOT)
    set(ABLETON_LINK_ROOT "${CMAKE_SOURCE_DIR}/../link" CACHE PATH "Ableton Link repository root")
  endif()
  include(${ABLETON_LINK_ROOT}/AbletonLinkConfig.cmake)
  target_link_libraries(video_looper PRIVATE Ableton::Link)
  target_compile_definitions(video_looper PRIVATE USE_ABLETON_LINK)
endif()


