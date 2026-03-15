project(SRTPluginRE9_App)

set(LOCAL_EXPORT_NAME App)
set(RC_INTERNALNAME "SRTPluginRE9.Injector")
set(RC_ORIGINALFILENAME "${RC_INTERNALNAME}.exe")
configure_file(${CMAKE_SOURCE_DIR}/src/VersionInfo.rc.in ${PROJECT_BINARY_DIR}/VersionInfoApp.rc @ONLY)

add_executable(${PROJECT_NAME})
add_executable(SRTPluginRE9::App ALIAS ${PROJECT_NAME})

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

target_sources(${PROJECT_NAME}
    PRIVATE
        ${PROJECT_BINARY_DIR}/VersionInfoApp.rc
        ExeInjector.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
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
