project(SRTPluginRE9_Hook)

set(LOCAL_EXPORT_NAME Hook)
set(RC_INTERNALNAME "SRTPluginRE9")
set(RC_ORIGINALFILENAME "${RC_INTERNALNAME}.dll")
configure_file(${CMAKE_SOURCE_DIR}/src/VersionInfo.rc.in ${PROJECT_BINARY_DIR}/VersionInfoHook.rc @ONLY)

include(FetchContent)
set(FETCHCONTENT_QUIET off)

FetchContent_Declare(
    SHA-2
    GIT_REPOSITORY https://github.com/amosnier/sha-2.git
    GIT_TAG        565f65009bdd98267361b17d50cddd7c9beb3e6c # origin/master
    SOURCE_DIR extern/amosnier/sha-2
)
FetchContent_MakeAvailable(SHA-2)
FetchContent_GetProperties(SHA-2)

set(SAFETYHOOK_FETCH_ZYDIS ON)
FetchContent_Declare(
    safetyhook
    GIT_REPOSITORY https://github.com/cursey/safetyhook.git
    GIT_TAG        755a4a08aa7d0d7143331dc854235ebc2d1570f9 # origin/master
    SOURCE_DIR extern/cursey/safetyhook
)
FetchContent_MakeAvailable(safetyhook)
FetchContent_GetProperties(safetyhook)

# vcpkg dependencies.
find_package(imgui CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)

add_library(${PROJECT_NAME} SHARED)
add_library(SRTPluginRE9::Hook ALIAS ${PROJECT_NAME})

target_compile_options(${PROJECT_NAME} PUBLIC
    /W4                     # Warning level 4
    /WX                     # Treat warnings as errors
    /permissive-            # Conformance mode
    /Zc:__cplusplus         # Enable __cplusplus macro
    /EHsc                   # Exception handling model
    /sdl                    # Additional security checks
    /GS                     # Buffer security checks
    /guard:cf               # Control Flow Guard
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
)

target_sources(${PROJECT_NAME}
    PRIVATE
        ${PROJECT_BINARY_DIR}/VersionInfoHook.rc
        extern/amosnier/sha-2/sha-256.c
        VTableScanner.cpp
        AOBScanner.cpp
        DescriptorHeapAllocator.cpp
        UI.cpp
        Settings.cpp
        Render.cpp
        DX12Hook.cpp
        DInput8Hook.cpp
        Hook.cpp
        Logger.cpp
        Hash.cpp
        Process.cpp
        Thread.cpp
        GameVersion.cpp
        CrashHandler.cpp
        DllMain.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${sha-2_SOURCE_DIR}
        ${safetyhook_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC imgui::imgui
    PRIVATE safetyhook magic_enum::magic_enum d3d12 d3dcompiler dxgi dinput8 dxguid dbghelp psapi
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME ${RC_INTERNALNAME}
    EXPORT_NAME ${LOCAL_EXPORT_NAME}
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND $<$<OR:$<CONFIG:RELEASE>,$<CONFIG:MINSIZEREL>>:${CMAKE_STRIP}> ARGS --strip-all $<TARGET_FILE:${PROJECT_NAME}>)

install(TARGETS ${PROJECT_NAME}
    EXPORT SRTPluginRE9Targets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include
)