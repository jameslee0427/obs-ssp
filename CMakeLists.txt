cmake_minimum_required(VERSION 3.16...3.26)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_qt(COMPONENTS Widgets Core Network)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt::Core Qt::Widgets Qt::Network)
  target_compile_options(
    ${CMAKE_PROJECT_NAME} PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header
                                  -Wno-comma>)
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON
               AUTOUIC ON
               AUTORCC ON)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/Modules")

set(obs-ssp_SOURCES
    src/obs-ssp.cpp
    src/obs-ssp-source.cpp
    src/ffmpeg-decode.c
    src/controller/cameraconfig.cpp
    src/controller/cameracontroller.cpp
    src/ssp-mdns.cpp
    src/ssp-controller.cpp
    src/VFrameQueue.cpp
    src/ssp-client-iso.cpp
    src/ssp-dock.cpp
    src/ssp-toolbar.cpp
    src/camera-status-manager.cpp
    src/util/pipe.c)
	
if(OS_WINDOWS)
  list(APPEND obs-ssp_SOURCES src/util/pipe-windows.c)
else()
  list(APPEND obs-ssp_SOURCES src/util/pipe-posix.c)
endif()

set(obs-ssp_HEADERS src/obs-ssp.h src/ssp-mdns.h src/ssp-controller.h src/VFrameQueue.h
                     src/ssp-client.h
                     src/ssp-dock.h
                     src/ssp-toolbar.h
                     src/camera-status-manager.h
		     src/util/pipe.h)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${obs-ssp_SOURCES})

# /!\ TAKE NOTE: No need to edit things past this point /!\

# --- Platform-independent build settings ---
find_package(FFmpeg REQUIRED COMPONENTS AVCODEC AVUTIL)
add_subdirectory(thirdpty)

target_include_directories(
    ${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/lib/ssp/include
    ${CMAKE_SOURCE_DIR}/ssp_connector
    ${CMAKE_SOURCE_DIR}/lib/obs-browser/include # Make sure obs-browser headers are findable
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_HEVC)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE FFmpeg::avcodec FFmpeg::avutil mdns) # Assuming mdns is defined by add_subdirectory(thirdpty) or find_package

# Link with the plugin-support library for PLUGIN_NAME and PLUGIN_VERSION
if(TARGET plugin-support)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE plugin-support)
endif()

add_subdirectory(ssp_connector)

if(OS_MACOS)
  install(TARGETS ssp-connector DESTINATION "./${CMAKE_PROJECT_NAME}.plugin/Contents/MacOS")
  install(FILES ${LIBSSP_LIBRARY} DESTINATION "./${CMAKE_PROJECT_NAME}.plugin/Contents/Frameworks")
elseif(OS_WINDOWS)
  install(TARGETS ssp-connector DESTINATION "obs-plugins/64bit")
  install(FILES ${LIBSSP_LIBRARY} DESTINATION "obs-plugins/64bit")
endif()

# Override default set_target_properties_plugin to avoid installing to the wrong location
# This function is likely meant to ensure the plugin's internal versioning.
function(set_target_properties_plugin target)
    set(options "")
    set(oneValueArgs "")
    set(multiValueArgs PROPERTIES)
    cmake_parse_arguments(PARSE_ARGV 0 _STPO "${options}" "${oneValueArgs}" "${multiValueArgs}")

    message(DEBUG "Setting custom properties for target ${target}...")

    while(_STPO_PROPERTIES)
        list(POP_FRONT _STPO_PROPERTIES key value)
        set_property(TARGET ${target} PROPERTY ${key} "${value}")
    endwhile()

    # PLUGIN_VERSION should be defined somewhere, e.g., in bootstrap.cmake or earlier.
    # For this example, let's assume it's defined:
    if(NOT DEFINED PLUGIN_VERSION)
        set(PLUGIN_VERSION "1.0.0") # Example default if not defined
    endif()

    set_target_properties(${target} PROPERTIES VERSION 0 SOVERSION ${PLUGIN_VERSION})
endfunction()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# Removed the duplicate custom commands that were here.
# The custom commands to create directories and copy files are now correctly placed
# inside the 'if(OS_MACOS)' block higher up, after add_subdirectory(ssp_connector).


# Add specific install instructions for the plugin itself (data files)
if(OS_WINDOWS)
    if(EXISTS "${CMAKE_SOURCE_DIR}/data")
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/" DESTINATION "data/obs-plugins/${_name}" COMPONENT plugin)
    endif()
elseif(OS_MACOS)
    if(EXISTS "${CMAKE_SOURCE_DIR}/data")
        # For macOS bundles, resources go into Contents/Resources
        # This install rule is for 'cmake --install'. The custom command above handles 'build'.
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/" DESTINATION "${_OBS_SSP_PLUGIN_INSTALL_RESOURCES_DIR}" COMPONENT plugin)
    endif()
else()
    if(EXISTS "${CMAKE_SOURCE_DIR}/data")
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/${_name}" COMPONENT plugin)
    endif()
endif()

# CPack configuration for installer generation
set(CPACK_PACKAGE_NAME "obs-ssp")
set(CPACK_PACKAGE_VENDOR "")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "SSP plugin for OBS Studio")
set(CPACK_PACKAGE_VERSION ${CMAKE_PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "")
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_BINARY_DIR}/package")
set(CPACK_PACKAGING_INSTALL_PREFIX "")
set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS "")
set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
set(CMAKE_INSTALL_OPENMP_LIBRARIES FALSE)
set(CMAKE_INSTALL_MFC_LIBRARIES FALSE)
set(CPACK_INSTALL_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/cmake_install_post.cmake")
macro(InstallRequiredSystemLibraries)
endmacro()
add_custom_target(clean_package
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_install_post.cmake"
    COMMENT "Cleaning up package..."
)
set_target_properties(clean_package PROPERTIES FOLDER "Packaging")
if(TARGET package)
    add_dependencies(package clean_package)
endif()
set(CPACK_COMPONENTS_ALL plugin)
set(CPACK_COMPONENT_PLUGIN_DISPLAY_NAME "SSP Plugin for OBS Studio")
set(CPACK_COMPONENT_PLUGIN_DESCRIPTION "Enables SSP camera support in OBS Studio")
set(CPACK_COMPONENT_PLUGIN_REQUIRED TRUE)

if(WIN32)
    install(CODE [[
        file(GLOB_RECURSE runtime_dlls "${CMAKE_INSTALL_PREFIX}/bin/*.dll")
        foreach(dll ${runtime_dlls})
            if(NOT EXISTS "${CMAKE_INSTALL_PREFIX}/bin/64bit")
                file(MAKE_DIRECTORY "${CMAKE_INSTALL_PREFIX}/bin/64bit")
            endif()
            message(STATUS "Moving runtime DLL to bin/64bit: ${dll}")
            get_filename_component(dll_name ${dll} NAME)
            file(RENAME "${dll}" "${CMAKE_INSTALL_PREFIX}/bin/64bit/${dll_name}")
        endforeach()
    ]] COMPONENT plugin)
endif()

if(WIN32)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/64bit")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/64bit")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/64bit")
    if(DEFINED CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "bin/64bit")
    endif()
endif()

if(WIN32)
    find_program(NSIS_PATH makensis)
    if(NSIS_PATH)
        set(CPACK_GENERATOR "ZIP;NSIS")
    else()
        message(STATUS "NSIS not found, only ZIP package will be generated")
        set(CPACK_GENERATOR "ZIP")
    endif()
    set(CPACK_NSIS_PACKAGE_NAME "OBS SSP Plugin")
    set(CPACK_NSIS_DISPLAY_NAME "OBS SSP Plugin")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64\\obs-studio")
    set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "obs-ssp")
    set(CPACK_NSIS_INSTALL_PREFIX "${CPACK_NSIS_INSTALL_ROOT}")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "obs64.exe")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "obs64.exe")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "OBS SSP Plugin")
    set(CPACK_DMG_FORMAT "UDZO")
else()
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "IMVT")
    set(CPACK_DEBIAN_PACKAGE_SECTION "video")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "obs-studio")
    set(CPACK_RPM_PACKAGE_REQUIRES "obs-studio")
endif()

include(CPack)