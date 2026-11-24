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
  app/src/io/DrawImage.mm
  app/src/io/PageCache.cpp
  app/src/io/SourceLoad.cpp
  app/src/io/FolderList.cpp
  app/src/io/VideoThumbs.mm
  app/src/util/MacFileDialog.mm
  app/src/remote/RemoteSession.cpp
  app/src/remote/RemoteApply.cpp
  app/src/remote/UsageLog.cpp
  app/src/remote/QrCode.cpp
  app/src/setup/SetupStore.cpp
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(video_looper PRIVATE app/src ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(video_looper PRIVATE SHADER_DIR="${CMAKE_SOURCE_DIR}/app/shaders" WEB_DIR="${CMAKE_SOURCE_DIR}/app/web")

target_link_libraries(video_looper PRIVATE glfw)

add_custom_command(TARGET video_looper POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/app/web $<TARGET_BUNDLE_CONTENT_DIR:video_looper>/Resources/web
)

add_executable(remote_session_test
  app/tests/remote_session_test.cpp
  app/src/remote/RemoteSession.cpp
  app/src/remote/RemoteApply.cpp
  app/src/remote/UsageLog.cpp
  app/src/ui/ParamBus.cpp
  app/src/io/SourceLoad.cpp
  app/src/io/FolderList.cpp
)
target_include_directories(remote_session_test PRIVATE app/src)
target_compile_definitions(remote_session_test PRIVATE WEB_DIR="${CMAKE_SOURCE_DIR}/app/web")

add_executable(usage_log_test
  app/tests/usage_log_test.cpp
  app/src/remote/UsageLog.cpp
)
target_include_directories(usage_log_test PRIVATE app/src)

add_executable(draw_image_test
  app/tests/draw_image_test.cpp
  app/src/io/DrawImage.mm
)
target_include_directories(draw_image_test PRIVATE app/src)
target_compile_definitions(draw_image_test PRIVATE DRAW_PNG="${CMAKE_SOURCE_DIR}/app/tests/draw.png")

add_executable(contain_rect_test app/tests/contain_rect_test.cpp)
target_include_directories(contain_rect_test PRIVATE app/src)

add_executable(repeat_test app/tests/repeat_test.cpp)
target_include_directories(repeat_test PRIVATE app/src)

add_executable(setup_test
  app/tests/setup_test.cpp
  app/src/setup/SetupStore.cpp
  app/src/ui/ParamBus.cpp
  app/src/io/FolderList.cpp
  app/src/remote/RemoteApply.cpp
)
target_include_directories(setup_test PRIVATE app/src)

add_custom_target(gestures_test
  COMMAND node ${CMAKE_SOURCE_DIR}/app/tests/gestures_test.js
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  VERBATIM
)

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
  find_library(IMAGEIO_FRAMEWORK ImageIO)
  find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
  target_link_libraries(video_looper PRIVATE
    ${COCOA_FRAMEWORK}
    ${APPKIT_FRAMEWORK}
    ${IOKIT_FRAMEWORK}
    ${COREFOUNDATION_FRAMEWORK}
    ${COREVIDEO_FRAMEWORK}
    ${AVFOUNDATION_FRAMEWORK}
    ${COREMEDIA_FRAMEWORK}
    ${OPENGL_FRAMEWORK}
    ${IMAGEIO_FRAMEWORK}
    ${COREGRAPHICS_FRAMEWORK}
  )
  target_link_libraries(draw_image_test PRIVATE
    ${IMAGEIO_FRAMEWORK}
    ${COREGRAPHICS_FRAMEWORK}
    ${COREFOUNDATION_FRAMEWORK}
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


