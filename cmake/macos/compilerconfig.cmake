include_guard(GLOBAL)

option(ENABLE_COMPILER_TRACE "Enable clang time-trace" OFF)
mark_as_advanced(ENABLE_COMPILER_TRACE)

if(NOT XCODE)
  message(FATAL_ERROR "Building OBS Studio on macOS requires Xcode generator.")
endif()

include(ccache)
include(compiler_common)

add_compile_options("$<$<NOT:$<COMPILE_LANGUAGE:Swift>>:-fopenmp-simd>")

function(check_sdk_requirements)
  set(obs_macos_minimum_sdk 15.0)
  set(obs_macos_minimum_xcode 16.0)
  execute_process(
    COMMAND xcrun --sdk macosx --show-sdk-platform-version
    OUTPUT_VARIABLE obs_macos_current_sdk
    ERROR_VARIABLE error_output
    RESULT_VARIABLE result
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  message(STATUS "xcrun result: ${result}")
  message(STATUS "xcrun output: '${obs_macos_current_sdk}'")
  message(STATUS "xcrun error: '${error_output}'")
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Failed to fetch macOS SDK version. Result: ${result}, Error: ${error_output}. Ensure macOS SDK is installed and xcode-select points to Xcode developer directory.")
  endif()
  if("${obs_macos_current_sdk}" STREQUAL "")
    message(FATAL_ERROR "macOS SDK version is empty. Ensure xcrun is configured correctly.")
  endif()
  message(STATUS "macOS SDK version: ${obs_macos_current_sdk}")
  if(obs_macos_current_sdk VERSION_LESS obs_macos_minimum_sdk)
    message(FATAL_ERROR "Your macOS SDK version (${obs_macos_current_sdk}) is too low. The macOS ${obs_macos_minimum_sdk} SDK (Xcode ${obs_macos_minimum_xcode}) is required to build OBS.")
  endif()
  execute_process(
    COMMAND xcrun --find xcodebuild
    OUTPUT_VARIABLE obs_macos_xcodebuild
    RESULT_VARIABLE result
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Xcode was not found. Ensure you have installed Xcode and xcode-select points to Xcode developer directory.")
  endif()
  message(DEBUG "Path to xcodebuild binary: ${obs_macos_xcodebuild}")
  if(XCODE_VERSION VERSION_LESS obs_macos_minimum_xcode)
    message(FATAL_ERROR "Your Xcode version (${XCODE_VERSION}) is too low. Xcode ${obs_macos_minimum_xcode} is required to build OBS.")
  endif()
endfunction()

check_sdk_requirements()

string(APPEND CMAKE_C_FLAGS_RELEASE " -g")
string(APPEND CMAKE_CXX_FLAGS_RELEASE " -g")
string(APPEND CMAKE_OBJC_FLAGS_RELEASE " -g")
string(APPEND CMAKE_OBJCXX_FLAGS_RELEASE " -g")

add_compile_definitions(
  $<$<NOT:$<COMPILE_LANGUAGE:Swift>>:$<$<CONFIG:DEBUG>:DEBUG>>
  $<$<NOT:$<COMPILE_LANGUAGE:Swift>>:$<$<CONFIG:DEBUG>:_DEBUG>>
  $<$<NOT:$<COMPILE_LANGUAGE:Swift>>:SIMDE_ENABLE_OPENMP>
)

if(ENABLE_COMPILER_TRACE)
  add_compile_options(
    $<$<NOT:$<COMPILE_LANGUAGE:Swift>>:-ftime-trace>
    "$<$<COMPILE_LANGUAGE:Swift>:SHELL:-Xfrontend -debug-time-expression-type-checking>"
    "$<$<COMPILE_LANGUAGE:Swift>:SHELL:-Xfrontend -debug-time-function-bodies>"
  )
  add_link_options(LINKER:-print_statistics)
endif()