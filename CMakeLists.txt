cmake_minimum_required(VERSION 3.16...3.26)

cmake_policy(SET CMP0010 NEW)
cmake_policy(SET CMP0177 NEW)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -v")
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)
#CMAKE_OSX_SYSROOT  /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX15.5.sdk
#CMAKE_OSX_ARCHITECTURES arm64
# Define project name and version, assuming defaults if not set by bootstrap.cmake
if(NOT DEFINED _name)
    set(_name "obs-ssp")
endif()
if(NOT DEFINED _version)
    set(_version "1.0.0")
endif()

# Define paths for the macOS bundle structure
if(APPLE)
    set(_OBS_SSP_PLUGIN_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/${CMAKE_PROJECT_NAME}.plugin")
    set(_OBS_SSP_PLUGIN_BUILD_CONTENTS_DIR "${_OBS_SSP_PLUGIN_BUILD_DIR}/Contents")
    set(_OBS_SSP_PLUGIN_BUILD_MACOS_DIR "${_OBS_SSP_PLUGIN_BUILD_CONTENTS_DIR}/MacOS")
    set(_OBS_SSP_PLUGIN_BUILD_FRAMEWORKS_DIR "${_OBS_SSP_PLUGIN_BUILD_CONTENTS_DIR}/Frameworks")
    set(_OBS_SSP_PLUGIN_BUILD_RESOURCES_DIR "${_OBS_SSP_PLUGIN_BUILD_CONTENTS_DIR}/Resources")
    set(_OBS_SSP_PLUGIN_BUILD_INFO_PLIST "${_OBS_SSP_PLUGIN_BUILD_CONTENTS_DIR}/Info.plist")

    set(_OBS_SSP_PLUGIN_INSTALL_BASE_DIR "obs-plugins/mac-os/${CMAKE_PROJECT_NAME}.plugin")
    set(_OBS_SSP_PLUGIN_INSTALL_CONTENTS_DIR "${CMAKE_INSTALL_PREFIX}/${_OBS_SSP_PLUGIN_INSTALL_BASE_DIR}/Contents")
    set(_OBS_SSP_PLUGIN_INSTALL_MACOS_DIR "${_OBS_SSP_PLUGIN_INSTALL_CONTENTS_DIR}/MacOS")
    set(_OBS_SSP_PLUGIN_INSTALL_FRAMEWORKS_DIR "${_OBS_SSP_PLUGIN_INSTALL_CONTENTS_DIR}/Frameworks")
    set(_OBS_SSP_PLUGIN_INSTALL_RESOURCES_DIR "${_OBS_SSP_PLUGIN_INSTALL_CONTENTS_DIR}/Resources")
    
    set(CODESIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/entitlements.plist")
endif()

project(${_name} VERSION ${_version})

set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

# Add the main plugin library
add_library(${CMAKE_PROJECT_NAME} MODULE)

# --- Core Modification for macOS Bundle Naming ---
if(APPLE)
    set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        BUNDLE TRUE
        OUTPUT_NAME "obs-ssp" # Ensures the binary inside the bundle is "obs-ssp"
        BUNDLE_EXTENSION "plugin"
    )
endif()
# --- End Core Modification ---



#find_package(libobs REQUIRED)


#target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)
find_path(LIBOBS_INCLUDE_DIR obs.h HINTS ${CMAKE_SOURCE_DIR}/.deps/Frameworks/libobs.framework/Headers)
if(NOT LIBOBS_INCLUDE_DIR)
    message(FATAL_ERROR "obs.h not found. Ensure OBS Studio source is at ~/obs-studio or install the OBS SDK.")
endif()
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${LIBOBS_INCLUDE_DIR})

find_library(LIBOBS_FRAMEWORK libobs HINTS /Applications/OBS.app/Contents/Frameworks)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${LIBOBS_FRAMEWORK})

if(ENABLE_FRONTEND_API)
    find_package(obs-frontend-api REQUIRED)
    find_package(Qt6 COMPONENTS Widgets Core Network REQUIRED)
    find_package(FFmpeg REQUIRED COMPONENTS AVCODEC AVUTIL)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
    if(NOT ENABLE_FRONTEND_API)
        find_package(Qt6 COMPONENTS Widgets Core Network REQUIRED)
    endif()
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

# --- Platform-independent build settings ---
find_package(FFmpeg REQUIRED COMPONENTS AVCODEC AVUTIL)
add_subdirectory(thirdpty) # Assuming this adds 'mdns' or other necessary targets/libraries


target_include_directories(
    ${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/lib/ssp/include
    ${CMAKE_SOURCE_DIR}/ssp_connector
    ${CMAKE_SOURCE_DIR}/lib/obs-browser/include # Make sure obs-browser headers are findable
)
if(os_windows)
target_link_libraries(${CMAKE_PROJECT_NAME} 
    PUBLIC
        optimized "${CMAKE_SOURCE_DIR}/lib/obs-browser/release/obs-browser.lib"
        debug "${CMAKE_SOURCE_DIR}/lib/obs-browser/debug/obs-browser.lib"
)
endif()
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_HEVC)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE FFmpeg::avcodec FFmpeg::avutil mdns) # Assuming mdns is defined by add_subdirectory(thirdpty) or find_package

# Link with the plugin-support library for PLUGIN_NAME and PLUGIN_VERSION
if(TARGET plugin-support)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE plugin-support)
endif()

add_subdirectory(ssp_connector)

if(OS_MACOS)
    add_dependencies(${CMAKE_PROJECT_NAME} ssp-connector)

    # Create directories
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_OBS_SSP_PLUGIN_BUILD_MACOS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E echo "Created MacOS directory: ${_OBS_SSP_PLUGIN_BUILD_MACOS_DIR}"
        VERBATIM
    )
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_OBS_SSP_PLUGIN_BUILD_FRAMEWORKS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E echo "Created Frameworks directory: ${_OBS_SSP_PLUGIN_BUILD_FRAMEWORKS_DIR}"
        VERBATIM
    )
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_OBS_SSP_PLUGIN_BUILD_RESOURCES_DIR}"
        COMMAND ${CMAKE_COMMAND} -E echo "Created Resources directory: ${_OBS_SSP_PLUGIN_BUILD_RESOURCES_DIR}"
        VERBATIM
    )

    # Copy Info.plist
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")
        add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist" "${_OBS_SSP_PLUGIN_BUILD_INFO_PLIST}"
            COMMAND ${CMAKE_COMMAND} -E echo "Copied Info.plist to ${_OBS_SSP_PLUGIN_BUILD_INFO_PLIST}"
            COMMENT "Copying Info.plist"
            VERBATIM
        )
    else()
        message(FATAL_ERROR "Info.plist not found at ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")
    endif()

    # Copy obs-ssp
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${_OBS_SSP_PLUGIN_BUILD_MACOS_DIR}/obs-ssp"
        COMMAND ${CMAKE_COMMAND} -E echo "Copied obs-ssp to ${_OBS_SSP_PLUGIN_BUILD_MACOS_DIR}/obs-ssp"
        COMMENT "Copying obs-ssp binary"
        VERBATIM
    )

    # Copy ssp-connector
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:ssp-connector>" "${_OBS_SSP_PLUGIN_BUILD_MACOS_DIR}/ssp-connector"
        COMMAND ${CMAKE_COMMAND} -E echo "Copied ssp-connector to ${_OBS_SSP_PLUGIN_BUILD_MACOS_DIR}/ssp-connector"
        COMMENT "Copying ssp-connector"
        VERBATIM
    )

    # Copy and sign libssp.dylib
    set(_LIBSSP_DYLIB_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/lib/ssp/Release/libssp.dylib")
    if(EXISTS "${_LIBSSP_DYLIB_SOURCE}")
        if(EXISTS "${CODESIGN_ENTITLEMENTS}")
            add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
                COMMAND /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/sign.sh" "${CODESIGN_ENTITLEMENTS}" "${_LIBSSP_DYLIB_SOURCE}" ""
                COMMAND ${CMAKE_COMMAND} -E echo "Signed libssp.dylib at ${_LIBSSP_DYLIB_SOURCE}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_LIBSSP_DYLIB_SOURCE}" "${_OBS_SSP_PLUGIN_BUILD_FRAMEWORKS_DIR}/libssp.dylib"
                COMMAND ${CMAKE_COMMAND} -E echo "Copied libssp.dylib to ${_OBS_SSP_PLUGIN_BUILD_FRAMEWORKS_DIR}/libssp.dylib"
                COMMENT "Signing and copying libssp.dylib"
                VERBATIM
            )
        else()
            message(WARNING "Entitlements file not found at ${CODESIGN_ENTITLEMENTS}. Skipping libssp.dylib signing.")
            add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_LIBSSP_DYLIB_SOURCE}" "${_OBS_SSP_PLUGIN_BUILD_FRAMEWORKS_DIR}/libssp.dylib"
                COMMAND ${CMAKE_COMMAND} -E echo "Copied libssp.dylib to ${_OBS_SSP_PLUGIN_BUILD_FRAMEWORKS_DIR}/libssp.dylib"
                COMMENT "Copying libssp.dylib without signing"
                VERBATIM
            )
        endif()
    else()
        message(FATAL_ERROR "libssp.dylib not found at ${_LIBSSP_DYLIB_SOURCE}")
    endif()

    # Copy data/
    if(EXISTS "${CMAKE_SOURCE_DIR}/data")
        add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data" "${_OBS_SSP_PLUGIN_BUILD_RESOURCES_DIR}"
            COMMAND ${CMAKE_COMMAND} -E echo "Copied data to ${_OBS_SSP_PLUGIN_BUILD_RESOURCES_DIR}"
            COMMENT "Copying data/"
            VERBATIM
        )
    else()
        message(WARNING "Data directory not found at ${CMAKE_SOURCE_DIR}/data")
    endif()

    # Sign binaries
    if(EXISTS "${CODESIGN_ENTITLEMENTS}")
        add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            COMMAND /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/sign.sh" "${CODESIGN_ENTITLEMENTS}" "${_OBS_SSP_PLUGIN_BUILD_MACOS_DIR}/obs-ssp" ""
            COMMAND ${CMAKE_COMMAND} -E echo "Signed obs-ssp binary"
            COMMAND /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/sign.sh" "${CODESIGN_ENTITLEMENTS}" "${_OBS_SSP_PLUGIN_BUILD_MACOS_DIR}/ssp-connector" ""
            COMMAND ${CMAKE_COMMAND} -E echo "Signed ssp-connector binary"
            COMMENT "Signing binaries"
            VERBATIM
        )
    else()
        message(WARNING "Entitlements file not found at ${CODESIGN_ENTITLEMENTS}. Skipping binary signing.")
    endif()

    # Sign bundle
    if(EXISTS "${CODESIGN_ENTITLEMENTS}")
        add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            COMMAND /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/sign.sh" "${CODESIGN_ENTITLEMENTS}" "${_OBS_SSP_PLUGIN_BUILD_DIR}" "--deep"
            COMMAND ${CMAKE_COMMAND} -E echo "Signed obs-ssp.plugin bundle"
            COMMENT "Signing obs-ssp.plugin bundle"
            VERBATIM
        )
    else()
        message(WARNING "Entitlements file not found at ${CODESIGN_ENTITLEMENTS}. Skipping bundle signing.")
    endif()
endif()

# Installation rules for ssp-connector
if(OS_MACOS)
    install(TARGETS ${CMAKE_PROJECT_NAME}
            LIBRARY DESTINATION "${_OBS_SSP_PLUGIN_INSTALL_MACOS_DIR}"
            COMPONENT plugin)
    install(TARGETS ssp-connector
            DESTINATION "${_OBS_SSP_PLUGIN_INSTALL_MACOS_DIR}"
            COMPONENT plugin)
    if(EXISTS "${_LIBSSP_DYLIB_SOURCE}")
        install(FILES "${_LIBSSP_DYLIB_SOURCE}"
                DESTINATION "${_OBS_SSP_PLUGIN_INSTALL_FRAMEWORKS_DIR}"
                COMPONENT plugin)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
                DESTINATION "${_OBS_SSP_PLUGIN_INSTALL_CONTENTS_DIR}"
                COMPONENT plugin)
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/data")
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/"
                DESTINATION "${_OBS_SSP_PLUGIN_INSTALL_RESOURCES_DIR}"
                COMPONENT plugin)
    endif()
endif()

if(OS_MACOS)
    install(TARGETS ssp-connector
    DESTINATION "${_OBS_SSP_PLUGIN_INSTALL_MACOS_DIR}"
    COMPONENT plugin)

    if(EXISTS "${_LIBSSP_DYLIB_SOURCE}")
        install(FILES "${_LIBSSP_DYLIB_SOURCE}"
        DESTINATION "${_OBS_SSP_PLUGIN_INSTALL_FRAMEWORKS_DIR}"
        COMPONENT plugin)
    else()
        message(WARNING "libssp.dylib not found at ${_LIBSSP_DYLIB_SOURCE} for installation.")
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
        DESTINATION "${_OBS_SSP_PLUGIN_INSTALL_CONTENTS_DIR}"
        COMPONENT plugin)
    else()
        message(WARNING "Info.plist not found at ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist for installation.")
    endif()

elseif(OS_WINDOWS)
    install(TARGETS ssp-connector
        DESTINATION "obs-plugins/64bit"
        COMPONENT plugin)
    # Assuming LIBSSP_LIBRARY is defined for Windows, if applicable
    install(FILES ${LIBSSP_LIBRARY}
        DESTINATION "obs-plugins/64bit"
        COMPONENT plugin)
else()
    # For Linux or other platforms
    install(TARGETS ssp-connector
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-plugins"
        COMPONENT plugin)
endif()

# Platform-specific installation rules for the main target
# This block is *very* important for the plugin output location.
if(OS_MACOS)
    install(TARGETS ${CMAKE_PROJECT_NAME}
        LIBRARY DESTINATION "obs-plugins/mac-os" # This installs the .plugin bundle here
        COMPONENT plugin)
elseif(OS_WINDOWS)
    install(TARGETS ${CMAKE_PROJECT_NAME}
        RUNTIME DESTINATION "obs-plugins/64bit"
        LIBRARY DESTINATION "obs-plugins/64bit" # Explicitly specify LIBRARY DESTINATION
        COMPONENT plugin)
else()
    install(TARGETS ${CMAKE_PROJECT_NAME}
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-plugins"
        COMPONENT plugin)
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