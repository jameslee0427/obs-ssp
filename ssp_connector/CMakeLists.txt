project(ssp-connector)

include(FetchContent)
#FetchContent_Declare(
 # libssp
 # GIT_REPOSITORY "https://github.com/imaginevision/libssp.git"
 # GIT_TAG ee754be1d2ed54633ff467a7cd6fec14c9b0b537)

#if(NOT libssp_POPULATED)
#  FetchContent_MakeAvailable(libssp)
  add_library(libssp INTERFACE)
 # target_include_directories(libssp INTERFACE ${libssp_SOURCE_DIR}/include)
  if(OS_MACOS)
    set(LIBSSP_DYLIB "${CMAKE_CURRENT_LIST_DIR}/../lib/ssp/Release/libssp.dylib")
    target_link_libraries(libssp INTERFACE ${LIBSSP_DYLIB})
    set(LIBSSP_LIBRARY "${LIBSSP_DYLIB}" PARENT_SCOPE)
    if(EXISTS "${LIBSSP_DYLIB}")
        set(_ENTITLEMENTS "${CMAKE_CURRENT_LIST_DIR}/../entitlements.plist")
        if(EXISTS "${_ENTITLEMENTS}")
            set(_COMMAND
                "/usr/bin/codesign --force \
                --sign \"${CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY}\" \
                --options runtime \
                --entitlements \"${_ENTITLEMENTS}\" \
                \"${LIBSSP_DYLIB}\"")
            execute_process(COMMAND /bin/sh -c "${_COMMAND}" RESULT_VARIABLE SIGN_RESULT)
            if(NOT SIGN_RESULT EQUAL 0)
                message(WARNING "Failed to sign libssp.dylib: ${SIGN_RESULT}")
            endif()
        else()
            message(WARNING "Entitlements file not found at ${_ENTITLEMENTS}. Skipping signing.")
        endif()
    else()
        message(FATAL_ERROR "libssp.dylib not found at ${LIBSSP_DYLIB}")
    endif()
  elseif(OS_WINDOWS)
 #   target_link_libraries(
 #     libssp INTERFACE "${libssp_SOURCE_DIR}/lib/win_x64_vs2017/libssp$<$<CONFIG:Debug>:d>.lib")
 #   set(LIBSSP_LIBRARY
 #       "${libssp_SOURCE_DIR}/lib/win_x64_vs2017/libssp$<$<CONFIG:Debug>:d>.dll"
  #      PARENT_SCOPE)
  endif()
#endif()

set(SSP_CONNECTOR_SOURCE main.cpp)
add_executable(ssp-connector ${SSP_CONNECTOR_SOURCE})
set_target_properties(ssp-connector PROPERTIES
    OSX_ARCHITECTURES arm64
    INSTALL_RPATH "@executable_path/../Frameworks"
    BUILD_WITH_INSTALL_RPATH TRUE
)
set_target_properties(ssp-connector PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ssp_connector/$<CONFIG>"
)
target_link_libraries(ssp-connector PRIVATE libssp)
target_include_directories(ssp-connector PRIVATE
    libuv/include
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${libssp_SOURCE_DIR}/include
)

if(OS_WINDOWS)
  target_compile_definitions(ssp-connector PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()