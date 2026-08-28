# -*- cmake -*-
#
# Compared to other libraries, compiling this one is a mess.  The
# reason is that we have several source files that have two different
# sets of behaviour, depending on whether they're intended to be part
# of the viewer or the map server.
#
# Unfortunately, the affected code is a rat's nest of #ifdefs, so it's
# easier to play compilation tricks than to actually fix the problem.

project(llwindow)

include(00-Common)
include(DragDrop)
include(Linking)
include(LLCommon)
include(LLImage)
include(LLWindow)
include(UI)
include(ViewerMiscLibs)
include(GLM)
include(SDL3)

if (LL_VULKAN_SDL_WSI AND NOT USE_SDL_WINDOW)
  message(FATAL_ERROR "LL_VULKAN_SDL_WSI requires the Linux SDL window path")
endif ()

if (LL_VULKAN_MACOS_WSI)
  if (NOT DARWIN)
    message(FATAL_ERROR "LL_VULKAN_MACOS_WSI is supported only on macOS")
  endif ()
  if (NOT LL_TESTS OR NOT LL_VULKAN_RUNTIME_TEST)
    message(FATAL_ERROR "LL_VULKAN_MACOS_WSI requires LL_TESTS=ON and LL_VULKAN_RUNTIME_TEST=ON")
  endif ()
endif ()

set(llwindow_SOURCE_FILES
    llcursortypes.cpp
    llkeyboard.cpp
    llkeyboardheadless.cpp
    llwindowheadless.cpp
    llwindowcallbacks.cpp
    llwindow.cpp
    llwindowvulkanrequirements.cpp
    )

set(llwindow_HEADER_FILES
    CMakeLists.txt

    llcursortypes.h
    llkeyboard.h
    llkeyboardheadless.h
    llwindowheadless.h
    llwindowcallbacks.h
    llwindowvulkanrequirements.h
    )

set(viewer_SOURCE_FILES
    llmousehandler.cpp
    )

set(viewer_HEADER_FILES
    llwindow.h
    llpreeditor.h
    llmousehandler.h
    )

# Platform-specific files for IDE visibility (excluded from build on other platforms)
set(macosx_SOURCE_FILES
    llkeyboardmacosx.cpp
    llwindowmacosx.cpp
    llwindowmacosx-objc.mm
    llopenglview-objc.mm
    )

set(macosx_HEADER_FILES
    llkeyboardmacosx.h
    llwindowmacosx.h
    llwindowmacosx-objc.h
    llwindowmacosx_iokit.h
    llopenglview-objc.h
    llappdelegate-objc.h
    )

set(linux_SOURCE_FILES
    llsdl.cpp
    llkeyboardsdl.cpp
    llwindowsdl.cpp
    )

set(linux_HEADER_FILES
    llsdl.h
    llkeyboardsdl.h
    llwindowsdl.h
    )

set(windows_SOURCE_FILES
    llwindowwin32.cpp
    lldxhardware.cpp
    llkeyboardwin32.cpp
    lldragdropwin32.cpp
    )

set(windows_HEADER_FILES
    llwindowwin32.h
    lldxhardware.h
    llkeyboardwin32.h
    lldragdropwin32.h
    )

set(llwindow_LINK_LIBRARIES
        llcommon
        llimage
        llmath
        llrender
        llfilesystem
        llxml
        ll::glm
        ll::glext
        ll::uilibraries
        )

if (USE_SDL_WINDOW)
  list(APPEND viewer_SOURCE_FILES ${linux_SOURCE_FILES})
  list(APPEND viewer_HEADER_FILES ${linux_HEADER_FILES})

  if (LL_VULKAN_SDL_WSI)
    if (NOT LINUX)
      message(FATAL_ERROR "LL_VULKAN_SDL_WSI is supported only by the Linux SDL window path")
    endif ()
    if (NOT LL_TESTS OR NOT LL_VULKAN_RUNTIME_TEST)
      message(FATAL_ERROR "LL_VULKAN_SDL_WSI requires LL_TESTS=ON and LL_VULKAN_RUNTIME_TEST=ON")
    endif ()
    list(APPEND viewer_SOURCE_FILES llwindowsdlvulkan.cpp)
    list(APPEND viewer_HEADER_FILES llwindowsdlvulkan.h)
    list(APPEND llwindow_LINK_LIBRARIES llrendervulkaninstance)
  endif ()

  list(APPEND llwindow_LINK_LIBRARIES
       ll::SDL3
       )
endif ()

if (DARWIN)
  list(APPEND llwindow_SOURCE_FILES ${macosx_SOURCE_FILES})
  list(APPEND llwindow_HEADER_FILES ${macosx_HEADER_FILES})

  # We use a bunch of deprecated system APIs.
  set_source_files_properties(
      llkeyboardmacosx.cpp
      llwindowmacosx.cpp
      PROPERTIES
      COMPILE_FLAGS "-fpascal-strings"
      )

  set_source_files_properties(
    llwindowmacosx-objc.mm
    llopenglview-objc.mm
    PROPERTIES
    SKIP_PRECOMPILE_HEADERS TRUE
  )
endif (DARWIN)


if (WINDOWS)
  list(APPEND llwindow_SOURCE_FILES ${windows_SOURCE_FILES})
  list(APPEND llwindow_HEADER_FILES ${windows_HEADER_FILES})
  list(APPEND llwindow_LINK_LIBRARIES
       comdlg32     # Common Dialogs for ChooseColor
       ole32
       dxgi
       d3d9
       )
endif (WINDOWS)


if (BUILD_HEADLESS)
  set(llwindowheadless_SOURCE_FILES
      llcursortypes.cpp
      llkeyboard.cpp
      llkeyboardheadless.cpp
      llwindowheadless.cpp
      llwindowcallbacks.cpp
      llwindow.cpp
      llwindowmesaheadless.cpp
      llmousehandler.cpp
      )

  set(llwindowheadless_HEADER_FILES
      llcursortypes.h
      llkeyboard.h
      llkeyboardheadless.h
      llwindowheadless.h
      llwindowcallbacks.h
      llwindowmesaheadless.h
      llmousehandler.h
      )

  add_library (llwindowheadless
    ${llwindowheadless_SOURCE_FILES}
    ${llwindowheadless_HEADER_FILES}
    )

  target_compile_definitions( llwindowheadless PUBLIC LL_MESA_HEADLESS=1 )

  target_link_libraries (llwindowheadless
  llcommon
  llimage
  llmath
  llrenderheadless
  llfilesystem
  llxml
  ll::glm
  ll::glext
  ll::zlib-ng
  PkgConfig::OSMESA
  fontconfig
  dl
  )
  target_include_directories(llwindowheadless INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
  
  if (USE_PRECOMPILED_HEADERS)
    target_precompile_headers(llwindowheadless REUSE_FROM llprecompiled)
  endif ()
endif (BUILD_HEADLESS)

if (llwindow_HEADER_FILES)
  list(APPEND llwindow_SOURCE_FILES ${llwindow_HEADER_FILES})
endif (llwindow_HEADER_FILES)

  list(APPEND viewer_SOURCE_FILES ${viewer_HEADER_FILES})

  # Collect all platform files for IDE visibility, excluding already-added current platform
  set(ide_visibility_files "")
  if (NOT DARWIN)
    list(APPEND ide_visibility_files ${macosx_SOURCE_FILES} ${macosx_HEADER_FILES})
  endif()
  if (NOT LINUX)
    list(APPEND ide_visibility_files ${linux_SOURCE_FILES} ${linux_HEADER_FILES})
  endif()
  if (NOT WINDOWS)
    list(APPEND ide_visibility_files ${windows_SOURCE_FILES} ${windows_HEADER_FILES})
  endif()

  add_library (llwindow
    ${llwindow_SOURCE_FILES}
    ${viewer_SOURCE_FILES}
    # Add non-platform files for IDE visibility only
    ${ide_visibility_files}
    )

# Mark non-platform files as excluded from build
if (WINDOWS)
  set_source_files_properties(
    ${macosx_SOURCE_FILES}
    ${linux_SOURCE_FILES}
    PROPERTIES
    HEADER_FILE_ONLY TRUE
    )
elseif (DARWIN)
  set_source_files_properties(
    ${windows_SOURCE_FILES}
    ${linux_SOURCE_FILES}
    PROPERTIES
    HEADER_FILE_ONLY TRUE
    )
elseif (LINUX)
  set_source_files_properties(
    ${macosx_SOURCE_FILES}
    ${windows_SOURCE_FILES}
    PROPERTIES
    HEADER_FILE_ONLY TRUE
    )
endif()

target_link_libraries (llwindow ${llwindow_LINK_LIBRARIES})
target_include_directories(llwindow INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

if (USE_SDL_WINDOW)
  target_compile_definitions( llwindow PUBLIC LL_SDL_WINDOW=1 )
endif()

if (LL_VULKAN_SDL_WSI)
  target_compile_definitions(llwindow PUBLIC LL_VULKAN_SDL_WSI=1)
endif ()

if (USE_PRECOMPILED_HEADERS)
  target_precompile_headers(llwindow REUSE_FROM llprecompiled)
endif ()

if (DARWIN)
  target_link_libraries(llwindow ${CARBON_LIBRARY})
endif (DARWIN)

if (LL_VULKAN_MACOS_WSI)
  find_library(LL_VULKAN_QUARTZCORE_LIBRARY QuartzCore)
  if (NOT LL_VULKAN_QUARTZCORE_LIBRARY)
    message(FATAL_ERROR "LL_VULKAN_MACOS_WSI requires the QuartzCore framework")
  endif ()

  add_library(llwindowmacosxvulkan STATIC
    llwindowmacosxvulkan.cpp
    llwindowmacosxvulkan.h
    llwindowmacosxvulkan-objc.mm
    llwindowmacosxvulkan-objc.h)
  target_include_directories(llwindowmacosxvulkan
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(llwindowmacosxvulkan
    PUBLIC
      llwindow
      llrendervulkaninstance
    PRIVATE
      ${APPKIT_LIBRARY}
      ${LL_VULKAN_QUARTZCORE_LIBRARY}
      ${CMAKE_DL_LIBS})
  set_target_properties(llwindowmacosxvulkan PROPERTIES FOLDER "Tests")
  set_source_files_properties(
    llwindowmacosxvulkan.cpp
    PROPERTIES
      COMPILE_DEFINITIONS "VK_NO_PROTOTYPES;VK_USE_PLATFORM_METAL_EXT")
  set_source_files_properties(
    llwindowmacosxvulkan-objc.mm
    PROPERTIES
      SKIP_PRECOMPILE_HEADERS TRUE)
endif ()

if (LL_TESTS)
  include(LLAddBuildTest)
  set_source_files_properties(
    tests/llwindowgraphicsapi_test.cpp
    tests/llwindowvulkanrequirements_test.cpp
    PROPERTIES
    SKIP_PRECOMPILE_HEADERS TRUE
  )
  LL_ADD_INTEGRATION_TEST(llwindowgraphicsapi "" "llwindow;llcommon")
  LL_ADD_INTEGRATION_TEST(llwindowvulkanrequirements "" "llwindow;llcommon")

  if (LL_VULKAN_SDL_WSI)
    set_source_files_properties(
      tests/llwindowsdlvulkan_test.cpp
      tests/llwindowvulkansdlwsi_test.cpp
      PROPERTIES
      SKIP_PRECOMPILE_HEADERS TRUE
    )
    LL_ADD_INTEGRATION_TEST(llwindowsdlvulkan "" "llwindow;llcommon")
    LL_ADD_INTEGRATION_TEST(
      llwindowvulkansdlwsi
      ""
      "llwindow;llrendervulkaninstance;llrendervulkanglobaldispatch;llcommon")
    target_compile_definitions(
      INTEGRATION_TEST_llwindowvulkansdlwsi
      PRIVATE VK_NO_PROTOTYPES)
  endif ()

  if (LL_VULKAN_MACOS_WSI)
    set_source_files_properties(
      tests/llwindowmacosxvulkan_test.cpp
      tests/llwindowvulkanmacoswsi_test.cpp
      PROPERTIES
        SKIP_PRECOMPILE_HEADERS TRUE)
    LL_ADD_INTEGRATION_TEST(
      llwindowmacosxvulkan
      ""
      "llwindowmacosxvulkan;llcommon")
    LL_ADD_INTEGRATION_TEST(
      llwindowvulkanmacoswsi
      ""
      "llwindowmacosxvulkan;llcommon")
    target_compile_definitions(
      INTEGRATION_TEST_llwindowmacosxvulkan
      PRIVATE VK_NO_PROTOTYPES)
    target_compile_definitions(
      INTEGRATION_TEST_llwindowvulkanmacoswsi
      PRIVATE VK_NO_PROTOTYPES)
  endif ()
endif ()
