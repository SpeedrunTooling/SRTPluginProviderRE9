cmake_minimum_required(VERSION 3.31)
project (SRTPluginRE9 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

#set(CMAKE_C_STANDARD 23) # 90, 99, 11, 17, 23
#set(CMAKE_C_STANDARD_REQUIRED ON)
#set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)  # 98, 11, 14, 17, 20, 23, 26
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".dll")
set(CMAKE_STATIC_LIBRARY_PREFIX "")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".lib")
set(CMAKE_EXECUTABLE_ENABLE_EXPORTS TRUE)

#set(VCPKG_C_FLAGS ${VCPKG_C_FLAGS} /DIM_ASSERT\(_EXPR\)=\(\(void\)\(_EXPR\)\))
set(VCPKG_CXX_FLAGS ${VCPKG_CXX_FLAGS} /DIM_ASSERT\(_EXPR\)=\(\(void\)\(_EXPR\)\))

# Release optimizations
add_compile_options(
    "$<$<OR:$<CONFIG:RELEASE>,$<CONFIG:MINSIZEREL>,$<CONFIG:RELWITHDEBINFO>>:/O2>"
    "$<$<OR:$<CONFIG:RELEASE>,$<CONFIG:MINSIZEREL>,$<CONFIG:RELWITHDEBINFO>>:/Ob2>"
    "$<$<OR:$<CONFIG:RELEASE>,$<CONFIG:MINSIZEREL>,$<CONFIG:RELWITHDEBINFO>>:/Oi>"
    "$<$<OR:$<CONFIG:RELEASE>,$<CONFIG:MINSIZEREL>,$<CONFIG:RELWITHDEBINFO>>:/Ot>"
    "$<$<OR:$<CONFIG:RELEASE>,$<CONFIG:MINSIZEREL>,$<CONFIG:RELWITHDEBINFO>>:/GL>"
)
add_link_options(
    "$<$<OR:$<CONFIG:RELEASE>,$<CONFIG:MINSIZEREL>,$<CONFIG:RELWITHDEBINFO>>:/LTCG>"
    "$<$<OR:$<CONFIG:RELEASE>,$<CONFIG:MINSIZEREL>,$<CONFIG:RELWITHDEBINFO>>:/INCREMENTAL:NO>"
    "$<$<OR:$<CONFIG:RELEASE>,$<CONFIG:MINSIZEREL>,$<CONFIG:RELWITHDEBINFO>>:/OPT:REF>"
    "$<$<OR:$<CONFIG:RELEASE>,$<CONFIG:MINSIZEREL>,$<CONFIG:RELWITHDEBINFO>>:/OPT:ICF>"
)
add_compile_definitions(
    "$<$<OR:$<CONFIG:RELEASE>,$<CONFIG:MINSIZEREL>,$<CONFIG:RELWITHDEBINFO>>:RELEASE>"
    "$<$<OR:$<CONFIG:DEBUG>,$<CONFIG:RELWITHDEBINFO>>:DEBUG>"
    "UNICODE"
    "_UNICODE"
    "WIN32_LEAN_AND_MEAN"
    "_AMD64_"
    "_WIN64"
)

function(set_from_env var_name default_value var_type var_desc)
  if(DEFINED ENV{${var_name}})
    set(${var_name} $ENV{${var_name}} CACHE ${var_type} ${var_desc} FORCE)
  else()
    set(${var_name} ${default_value} CACHE ${var_type} ${var_desc} FORCE)
  endif()
endfunction()

string(ASCII 169 COPYRIGHT)

# VERSIONINFO properties (Ref: https://learn.microsoft.com/en-us/windows/win32/menurc/versioninfo-resource)
set(RC_PRODUCTNAME "SRT Plugin for Resident Evil: Requiem (2026)")
set(RC_FILEDESCRIPTION "A speedrun tool plugin for Resident Evil: Requiem (2026).")
set(RC_COMPANYNAME "Squirrelies")
set(RC_LEGALCOPYRIGHT "Copyright ${COPYRIGHT} 2026 Squirrelies")

# Major.Minor.Patch[-PreRel]+Build-Hash
# Example: 0.1.0-rc.1+1-abcdef12
set_from_env(RC_VERSION_MAJOR 0 STRING "Version (major) of the product.")
set_from_env(RC_VERSION_MINOR 1 STRING "Version (minor) of the product.")
set_from_env(RC_VERSION_PATCH 0 STRING "Version (patch) of the product.")
set_from_env(RC_VERSION_PRERELEASE_TAG "" STRING "Version (build) of the product.")
set_from_env(RC_VERSION_BUILD_HASH "" STRING "Version (build) of the product.")
set_from_env(RC_VERSION_BUILD 1 STRING "Version (build) of the product.") # Auto-increment in build CI/CD.

set(RC_VERSION_SEMVER "${RC_VERSION_MAJOR}.${RC_VERSION_MINOR}.${RC_VERSION_PATCH}")
if(DEFINED RC_VERSION_PRERELEASE_TAG AND NOT RC_VERSION_PRERELEASE_TAG STREQUAL "")
    string(APPEND RC_VERSION_SEMVER "-${RC_VERSION_PRERELEASE_TAG}")
endif()
string(APPEND RC_VERSION_SEMVER "+${RC_VERSION_BUILD}")
if(DEFINED RC_VERSION_BUILD_HASH AND NOT RC_VERSION_BUILD_HASH STREQUAL "")
    string(APPEND RC_VERSION_SEMVER ".${RC_VERSION_BUILD_HASH}")
endif()

add_definitions("-DAPP_VERSION_MAJOR=${RC_VERSION_MAJOR}")
add_definitions("-DAPP_VERSION_MINOR=${RC_VERSION_MINOR}")
add_definitions("-DAPP_VERSION_PATCH=${RC_VERSION_PATCH}")
add_definitions("-DAPP_VERSION_PRERELEASE_TAG=\"${RC_VERSION_PRERELEASE_TAG}\"")
add_definitions("-DAPP_VERSION_BUILD_HASH=\"${RC_VERSION_BUILD_HASH}\"")
add_definitions("-DAPP_VERSION_BUILD=${RC_VERSION_BUILD}")
add_definitions("-DAPP_VERSION_SEMVER=\"${RC_VERSION_SEMVER}\"")

add_subdirectory(src/Hook)
add_subdirectory(src/App)
