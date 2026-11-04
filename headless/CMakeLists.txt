# rgon-cli: headless front-end for the launcher core.
#
# Reuses the upstream launcher sources that do not depend on wxWidgets and
# replaces the GUI with a JSON based command line interface (see README.md).
# Must be built for Win32, like the GUI launcher: the game is a 32-bit process.

file (READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" RGON_CLI_VERSION)
string (STRIP "${RGON_CLI_VERSION}" RGON_CLI_VERSION)

# Version of the launcher core, used by the Repentogon installer to honor the
# minimum launcher version required by a Repentogon release.
set (RGON_CLI_LAUNCHER_VERSION "${CMAKE_LAUNCHER_VERSION}")
if (NOT RGON_CLI_LAUNCHER_VERSION OR RGON_CLI_LAUNCHER_VERSION STREQUAL "dev")
    file (READ "${CMAKE_SOURCE_DIR}/version.txt" RGON_CLI_LAUNCHER_VERSION)
    string (STRIP "${RGON_CLI_LAUNCHER_VERSION}" RGON_CLI_LAUNCHER_VERSION)
endif()

message (STATUS "Building rgon-cli ${RGON_CLI_VERSION} (launcher core ${RGON_CLI_LAUNCHER_VERSION})")

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message (WARNING "rgon-cli must be built for Win32 (-A Win32) to start the 32-bit game")
endif()

set (RGON_CLI_CORE_SOURCES
    "${CMAKE_SOURCE_DIR}/src/cancel.cpp"
    "${CMAKE_SOURCE_DIR}/src/diff_patcher.cpp"
    "${CMAKE_SOURCE_DIR}/src/injector.cpp"
    "${CMAKE_SOURCE_DIR}/src/installation.cpp"
    "${CMAKE_SOURCE_DIR}/src/isaac_installation.cpp"
    "${CMAKE_SOURCE_DIR}/src/launcher_configuration.cpp"
    "${CMAKE_SOURCE_DIR}/src/loader.cpp"
    "${CMAKE_SOURCE_DIR}/src/repentogon_installation.cpp"
    "${CMAKE_SOURCE_DIR}/src/repentogon_installer.cpp"
    "${CMAKE_SOURCE_DIR}/src/standalone_rgon_folder.cpp"
    "${CMAKE_SOURCE_DIR}/src/steam_workshop.cpp"
    "${CMAKE_SOURCE_DIR}/src/version.cpp"
)

set (RGON_CLI_SOURCES
    src/cli_parser.cpp
    src/cli_parser.h
    src/encoding.cpp
    src/encoding.h
    src/json_log_gui.cpp
    src/json_log_gui.h
    src/main.cpp
    src/output.cpp
    src/output.h
)

# GUI subsystem so that no console window flashes when another program
# starts rgon-cli; output goes to the inherited stdout handle.
add_executable (rgon-cli WIN32 ${RGON_CLI_SOURCES} ${RGON_CLI_CORE_SOURCES} ${WIN32_RESOURCES})
set_target_properties (rgon-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/rgon-cli")

target_include_directories (rgon-cli PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/deps"
    "${CMAKE_SOURCE_DIR}/deps/curl/include"
    "${CMAKE_SOURCE_DIR}/deps/zlib"
    "${CMAKE_SOURCE_DIR}/deps/bzip2"
    "${CMAKE_SOURCE_DIR}/deps/bsdiff"
    "${CMAKE_SOURCE_DIR}/deps/steamapi"
    "${CMAKE_SOURCE_DIR}/deps/libzip/lib"
    "${CMAKE_SOURCE_DIR}/deps/rapidjson/include"
)

target_compile_definitions (rgon-cli PRIVATE
    LAUNCHER_HEADLESS
    NOMINMAX
    UNICODE
    INI_SHARED_LIB
    "CMAKE_LAUNCHER_VERSION=\"${RGON_CLI_LAUNCHER_VERSION}\""
    "RGON_CLI_VERSION=\"${RGON_CLI_VERSION}\""
)

target_compile_options (rgon-cli PRIVATE "/MD" ${MSVC_EXTRA_WARNINGS})

# The .trampo section copied into the game must not go through incremental
# linking thunks, and wmain is used as the entry point of a GUI executable.
target_link_options (rgon-cli PRIVATE "/INCREMENTAL:NO" "/ENTRY:wmainCRTStartup")

target_link_libraries (rgon-cli PRIVATE
    shared inih libcurl zip bzip2 bspatch steamapi
    Imagehlp bcrypt Winhttp userenv)

# Lay out a runnable folder next to the executable.
add_custom_command (TARGET rgon-cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:rgon-cli> $<TARGET_FILE_DIR:rgon-cli>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/deps/steamapi/lib/steam_appid.txt"
        $<TARGET_FILE_DIR:rgon-cli>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/patch"
        "$<TARGET_FILE_DIR:rgon-cli>/launcher-data/patch"
    COMMAND_EXPAND_LISTS
)

# Release package: cmake --install <build> --config Release --component rgon-cli --prefix <dir>
install (TARGETS rgon-cli RUNTIME DESTINATION . COMPONENT rgon-cli)
install (FILES $<TARGET_RUNTIME_DLLS:rgon-cli> DESTINATION . COMPONENT rgon-cli)
install (FILES
    "${CMAKE_SOURCE_DIR}/deps/steamapi/lib/steam_appid.txt"
    "${CMAKE_SOURCE_DIR}/LICENSE"
    DESTINATION . COMPONENT rgon-cli)
install (DIRECTORY "${CMAKE_SOURCE_DIR}/patch/" DESTINATION launcher-data/patch COMPONENT rgon-cli)

# Ship the (x86) Visual C++ runtime next to the executable so that rgon-cli
# does not depend on the x86 redistributable being installed.
set (CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
include (InstallRequiredSystemLibraries)
install (PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION . COMPONENT rgon-cli)
