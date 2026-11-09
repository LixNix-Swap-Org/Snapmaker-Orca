cmake_minimum_required(VERSION 3.13)
project(Snapmaker_Orca-native)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include dev-utils for encoding check and other utilities
add_subdirectory(dev-utils)
# add_subdirectory(avrdude)
# Note: semver and hints are now included from deps_src/CMakeLists.txt

find_package(libnoise REQUIRED)

# Add MQTT library
add_subdirectory(mqtt)

# --- Snapmaker Orca: vendored Paho MQTT -------------------------------------------------
# src/mqtt (Paho MQTT C++ and C, unedited) is built with warnings off like deps_src, so
# -Werror never stops inside third-party code.
if (NOT SLIC3R_BUNDLED_WARNINGS)
    orcaslicer_silence_third_party_warnings("${CMAKE_CURRENT_SOURCE_DIR}/mqtt")
endif ()
# --- end of vendored Paho MQTT ----------------------------------------------------------

# Set MQTT paths for use in slic3r GUI
set(PAHO_MQTT_CPP_PATH "${CMAKE_CURRENT_SOURCE_DIR}/mqtt" CACHE PATH "Paho MQTT C++ path")
set(PAHO_MQTT_C_PATH "${CMAKE_CURRENT_SOURCE_DIR}/mqtt/externals/paho-mqtt-c" CACHE PATH "Paho MQTT C path")

add_subdirectory(libslic3r)

if (SLIC3R_GUI)
    add_subdirectory(glad)
    # imgui, imguizmo, and hidapi are now included from deps_src
    add_subdirectory(libvgcode)

    if(WIN32)
        message(STATUS "WXWIN environment set to: $ENV{WXWIN}")
    elseif(UNIX)
        set(wxWidgets_USE_UNICODE ON)
        if(SLIC3R_STATIC)
            set(wxWidgets_USE_STATIC ON)
        else()
            set(wxWidgets_USE_STATIC OFF)
        endif()
    endif()

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set (wxWidgets_CONFIG_OPTIONS "--toolkit=gtk${SLIC3R_GTK}")
        find_package(wxWidgets 3.3 REQUIRED COMPONENTS base core adv html gl aui net media webview)
    else ()
        # propgrid is required by wxInspector.
        find_package(wxWidgets 3.3 CONFIG REQUIRED COMPONENTS html adv gl core base webview aui net media propgrid)
    endif ()

    if(UNIX)
        message(STATUS "wx-config path: ${wxWidgets_CONFIG_EXECUTABLE}")
    endif()

    if(wxWidgets_USE_FILE)
        include(${wxWidgets_USE_FILE})
    endif()

    if(APPLE AND TARGET wx::wxgl)
        # Newer wxWidgets CMake exports may inject the macOS OpenGL framework
        # as a quoted file path inside wx::wxgl, which Ninja then treats as a
        # missing input file. Orca links OpenGL explicitly elsewhere, so strip
        # these broken pseudo-paths from the imported target.
        get_target_property(_wx_gl_libs wx::wxgl INTERFACE_LINK_LIBRARIES)
        if(_wx_gl_libs)
            list(FILTER _wx_gl_libs EXCLUDE REGEX "framework OpenGL")
            set_target_properties(wx::wxgl PROPERTIES INTERFACE_LINK_LIBRARIES "${_wx_gl_libs}")
        endif()
    endif()

    find_package(JPEG QUIET)

    string(REGEX MATCH "wxpng" WX_PNG_BUILTIN ${wxWidgets_LIBRARIES})
    if (PNG_FOUND AND NOT WX_PNG_BUILTIN)
        list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX png)
        list(APPEND wxWidgets_LIBRARIES ${PNG_LIBRARIES})
    endif ()

    string(REGEX MATCH "wxjpeg" WX_JPEG_BUILTIN ${wxWidgets_LIBRARIES})
    if (JPEG_FOUND AND NOT WX_JPEG_BUILTIN)
        list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX jpeg)
        list(APPEND wxWidgets_LIBRARIES ${JPEG_LIBRARIES})
    endif ()

    string(REGEX MATCH "wxexpat" WX_EXPAT_BUILTIN ${wxWidgets_LIBRARIES})
    if (EXPAT_FOUND AND NOT WX_EXPAT_BUILTIN)
        list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX expat)
        list(APPEND wxWidgets_LIBRARIES ${EXPAT_LIBRARIES})
    endif ()

    # This is an issue in the new wxWidgets cmake build, doesn't deal with librt
    find_library(LIBRT rt)
    if(LIBRT)
        list(APPEND wxWidgets_LIBRARIES ${LIBRT})
    endif()

    # This fixes a OpenGL linking issue on OSX. wxWidgets cmake build includes
    # wrong libs for opengl in the link line and it does not link to it by himself.
    # libslic3r_gui will link to opengl anyway, so lets override wx
    list(FILTER wxWidgets_LIBRARIES EXCLUDE REGEX OpenGL)

#    list(REMOVE_ITEM wxWidgets_LIBRARIES oleacc)

    find_package(wxInspector REQUIRED)
    # wxInspector 1.0.0 installs its headers but accidentally declares the
    # INSTALL_INTERFACE include directory PRIVATE, so its imported target does
    # not expose them to consumers. Restore the package prefix include path until
    # the upstream export is fixed.
    get_filename_component(WXINSPECTOR_PREFIX "${wxInspector_DIR}/../../.." ABSOLUTE)
    target_include_directories(wxInspector::wxInspector INTERFACE "${WXINSPECTOR_PREFIX}/include")

    # wxInspector's exported interface names the release wxWidgets import
    # libraries, which a Debug build cannot link. wx is linked above instead.
    get_target_property(_wxinspector_interface wxInspector::wxInspector INTERFACE_LINK_LIBRARIES)
    if (_wxinspector_interface)
        list(FILTER _wxinspector_interface EXCLUDE REGEX "wx(base|msw)3[0-9]u[_.]")
        set_target_properties(wxInspector::wxInspector PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_wxinspector_interface}")
    endif ()

    list(APPEND wxWidgets_LIBRARIES "wxInspector::wxInspector")

    message(STATUS "wx libs: ${wxWidgets_LIBRARIES}")

    add_subdirectory(slic3r)
endif()

if(ORCA_TOOLS)
    # Snapmaker_Orca_profile_validator
    if(APPLE)
        add_executable(Snapmaker_Orca_profile_validator MACOSX_BUNDLE dev-utils/Snapmaker_Orca_profile_validator.cpp)
        set_target_properties(Snapmaker_Orca_profile_validator PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "Snapmaker_Orca Profile Validator"
            MACOSX_BUNDLE_BUNDLE_VERSION "${SLIC3R_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${SLIC3R_VERSION}"
            MACOSX_BUNDLE_IDENTIFIER "com.Snapmaker.Snapmaker_Orca.profile-validator"
            MACOSX_BUNDLE_COPYRIGHT "Copyright © 2024-2026 Snapmaker. All rights reserved."
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.Snapmaker.Snapmaker_Orca.profile-validator"
        )
    else()
        add_executable(Snapmaker_Orca_profile_validator dev-utils/Snapmaker_Orca_profile_validator.cpp)
    endif()
    target_link_libraries(Snapmaker_Orca_profile_validator libslic3r boost_headeronly libcurl OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(Snapmaker_Orca_profile_validator PRIVATE -DBOOST_ALL_NO_LIB -DBOOST_USE_WINAPI_VERSION=0x602 -DBOOST_SYSTEM_USE_UTF8)
    if(WIN32)
        target_link_libraries(Snapmaker_Orca_profile_validator bcrypt.lib)
    endif()
endif()

# -----------------------------------------------------------------------
# app_common: app-layer glue static library (sentry wrapper / product
# identity & machine info / crash stack / bury-point state).
# Sources live in src/app_common/ (moved from src/sentry_wrapper,
# src/bury_cfg, and dev-utils' BaseException/StackWalker).
# common_func stays in src/common_func/ (libslic3r etc. only consume its
# compile-time macros and must not depend back on app_common), but
# common_func.cpp is compiled here because SentryWrapper.obj references
# common:: symbols required by libslic3r_gui's symbol closure.
# Purpose: make libslic3r_gui's symbol closure self-contained - app-layer
# symbols are provided by this library so third-party consumers (tests,
# headless tools) can link the GUI library without stub files, the
# Snapmaker_Orca import library, or platform gating.
# Single supplier: every image (Snapmaker_Orca DLL/exe, the Windows shim,
# libslic3r_gui consumers) links this archive instead of compiling the
# glue sources directly, so each image holds exactly one copy of the glue
# symbols and no link-order / lazy-extraction invariant applies. Sources
# unique to this library can therefore be added safely.
# On Windows the archive is compiled with BURY_EXPORTS: members linked into
# an image export the accessors from that image (Snapmaker_Orca.dll and the
# shim each export their own copy; no cross-image import is introduced).
# Built unconditionally (not gated on SLIC3R_GUI): the headless build has
# no libslic3r_gui to carry the glue symbols, so the production exe must
# be able to link this library directly in the GUI=OFF configuration too.
# -----------------------------------------------------------------------
add_library(app_common STATIC
    app_common/SentryWrapper.hpp
    app_common/SentryWrapper.cpp
    common_func/common_func.hpp
    common_func/common_func.cpp
    app_common/bury_point.hpp
    app_common/bury_point.cpp
)
if (WIN32)
    target_sources(app_common PRIVATE
        app_common/BaseException.h
        app_common/BaseException.cpp
        app_common/StackWalker.h
        app_common/StackWalker.cpp
    )
endif ()
if (WIN32)
    # The archive members are the single bury-state copy in every image
    # that links this library; dllexport here reproduces the per-image
    # export surface the direct-compilation phase-1 targets provided
    # (Snapmaker_Orca.dll and the shim each export the accessors from
    # their own copy - no cross-image import is introduced).
    target_compile_definitions(app_common PRIVATE BURY_EXPORTS)
endif ()
# Headers are included as "app_common/..." relative to the src/ root:
# files directly under src/ (Snapmaker_Orca.cpp etc.) resolve them via
# quoted-include lookup in the includer's directory; GUI-internal files
# resolve them via the PUBLIC include dir below.
target_include_directories(app_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# common_func.hpp includes the libslic3r_version.h generated by libslic3r.
target_include_directories(app_common PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/libslic3r)
if (WIN32)
    # BaseException.cpp includes git_commit_hash.h, generated into the binary dir of src/slic3r.
    target_include_directories(app_common PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/slic3r)
    if (TARGET git_commit_hash_header)
        add_dependencies(app_common git_commit_hash_header)
    endif ()
    # BaseException.cpp calls std::max after including <Windows.h>.
    target_compile_definitions(app_common PRIVATE NOMINMAX)
endif ()
target_link_libraries(app_common PRIVATE boost_headeronly nlohmann_json)
if (WIN32)
    # common_func.cpp: RegOpenKeyEx / SHGetKnownFolderPath
    # (iphlpapi and version.lib are pulled in via in-source #pragma)
    # PRIVATE is the accurate scope (only common_func.cpp needs these);
    # on a STATIC library it still reaches consumers' link lines
    # (LINK_ONLY), matching what INTERFACE did here.
    target_link_libraries(app_common PRIVATE Advapi32 Shell32)
endif ()
if (SLIC3R_SENTRY)
    target_compile_definitions(app_common PRIVATE SLIC3R_SENTRY)
    target_link_libraries(app_common PRIVATE sentry::sentry)
endif ()
encoding_check(app_common)

# Create a slic3r executable
# Process manifests for various platforms.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/dev-utils/platform/msw/Snapmaker_Orca.rc.in ${CMAKE_CURRENT_BINARY_DIR}/Snapmaker_Orca.rc @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/dev-utils/platform/msw/Snapmaker_Orca.manifest.in ${CMAKE_CURRENT_BINARY_DIR}/Snapmaker_Orca.manifest @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/dev-utils/platform/osx/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)
if (WIN32)
    add_library(Snapmaker_Orca SHARED Snapmaker_Orca.cpp Snapmaker_Orca.hpp)
else ()
    add_executable(Snapmaker_Orca Snapmaker_Orca.cpp Snapmaker_Orca.hpp)
endif ()

if (MINGW)
    target_link_options(Snapmaker_Orca PUBLIC "-Wl,-allow-multiple-definition")
    set_target_properties(Snapmaker_Orca PROPERTIES PREFIX "")
endif (MINGW)

# The GUI embeds the bundled shared libpython (libslic3r_gui -> pybind11::embed);
# the dep recipe stamps it with an @rpath id / plain SONAME, so the executable
# needs an rpath entry for each layout in the "Bundled Python/uv layout" map
# further down. Dead entries are skipped by the loader.
# Snapmaker_Orca_profile_validator links libslic3r only (no Python).
if (APPLE)
    # A linker flag rather than the BUILD_RPATH property because the release
    # flow uses the Xcode generator, which does not honor BUILD_RPATH.
    target_link_options(Snapmaker_Orca PRIVATE "LINKER:-rpath,@executable_path/python/lib")
elseif (UNIX AND NOT WIN32)
    set_target_properties(Snapmaker_Orca PROPERTIES
        # Build tree + AppImage, which packages the build-tree binary.
        BUILD_RPATH "$ORIGIN/python/lib;$ORIGIN/../lib/python/lib"
        # Flatpak (cmake --install).
        INSTALL_RPATH "$ORIGIN/../libpython/lib")
endif ()

if (NOT WIN32 AND NOT APPLE)
    # Binary name on unix like systems (Linux, Unix)
    set_target_properties(Snapmaker_Orca PROPERTIES OUTPUT_NAME "snapmaker-orca")
    set(SLIC3R_APP_CMD "snapmaker-orca")
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/dev-utils/platform/unix/build_linux_image.sh.in ${CMAKE_CURRENT_BINARY_DIR}/build_linux_image.sh USE_SOURCE_PERMISSIONS @ONLY)
endif ()

target_link_libraries(Snapmaker_Orca libslic3r cereal::cereal)
# Glue symbols (sentry wrapper / bury-point / common_func) are supplied by
# the app_common archive instead of being compiled into this target; linked
# unconditionally (not only under SLIC3R_GUI) because the headless build has
# no libslic3r_gui to carry them transitively.
target_link_libraries(Snapmaker_Orca app_common)
# target_include_directories(OrcaSlicer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/slic3r)
if (APPLE)
#    add_compile_options(-stdlib=libc++)
#    add_definitions(-DBOOST_THREAD_DONT_USE_CHRONO -DBOOST_NO_CXX11_RVALUE_REFERENCES -DBOOST_THREAD_USES_MOVE)
    # -liconv: boost links to libiconv by default
    target_link_libraries(Snapmaker_Orca "-liconv -framework IOKit" "-framework CoreFoundation" "-framework AVFoundation" "-framework AVKit" "-framework CoreMedia" "-framework VideoToolbox" -lc++)

elseif (MSVC)
    # Manifest is provided through Snapmaker_Orca.rc, don't generate your own.
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")
else ()
    # Boost on Raspberry-Pi does not link to pthreads explicitly.
    target_link_libraries(Snapmaker_Orca ${CMAKE_DL_LIBS} -lstdc++ Threads::Threads pangoft2-1.0)
endif ()

if (SLIC3R_GUI)
#    target_link_libraries(Snapmaker_Orca ws2_32 uxtheme setupapi libslic3r_gui ${wxWidgets_LIBRARIES})
target_link_libraries(Snapmaker_Orca libslic3r_gui wxInspector::wxInspector)
# Building this target directly (as IDEs do) must still mirror the Snapmaker
# profiles into the source resources tree before the app runs.
add_dependencies(Snapmaker_Orca sync_snapmaker_profiles)
    if (MSVC)
        # Generate debug symbols even in release mode.
        target_link_options(Snapmaker_Orca PUBLIC "$<$<CONFIG:RELEASE>:/DEBUG>")
        target_link_libraries(Snapmaker_Orca ws2_32.lib user32.lib Setupapi.lib)
    elseif (MINGW)
        target_link_libraries(Snapmaker_Orca ws2_32 uxtheme setupapi)
    elseif (APPLE)
        target_link_libraries(Snapmaker_Orca "-framework OpenGL")
    else ()
        target_link_libraries(Snapmaker_Orca -ldl)
    endif ()
    #if (WIN32)
    #    find_library(PSAPI_LIB NAMES Psapi)
    #    target_link_libraries(Snapmaker_Orca ${PSAPI_LIB})
    #endif ()
endif ()

# Sentry crash reporting integration
if (SLIC3R_SENTRY)
    target_compile_definitions(Snapmaker_Orca PUBLIC SLIC3R_SENTRY)
    target_link_libraries(Snapmaker_Orca sentry::sentry)
endif()

# On Windows, a shim application is required to produce a console / non console version of the Slic3r application.
# Also the shim may load the Mesa software OpenGL renderer if the default renderer does not support OpenGL 2.0 and higher.
if (WIN32)
    if (MINGW)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -municode")
    endif()

    
    # The shim links the app_common archive (compiled with BURY_EXPORTS,
    # mirroring the Snapmaker_Orca DLL image) so it resolves its bury-point
    # helpers locally from its own copy instead of importing them from
    # Snapmaker_Orca.dll. The shim must
    # NOT statically depend on Snapmaker_Orca.dll: that DLL statically imports
    # opengl32.dll, which the loader maps BEFORE wmain runs - the system
    # opengl32.dll could then never be released and would forever shadow the
    # MESA opengl32.dll the shim loads for the software-OpenGL fallback
    # (see Snapmaker_Orca_app_msvc.cpp).
    add_executable(Snapmaker_Orca_app_gui WIN32 Snapmaker_Orca_app_msvc.cpp ${CMAKE_CURRENT_BINARY_DIR}/Snapmaker_Orca.rc)
    # Generate debug symbols even in release mode.
    if(MSVC)
        target_link_options(Snapmaker_Orca_app_gui PUBLIC "$<$<CONFIG:RELEASE>:/DEBUG>")
    endif()
    # The shim links the app_common archive (compiled with BURY_EXPORTS), so
    # its bury-point state is still self-hosted per-image and independent of
    # Snapmaker_Orca.dll (see the MESA/opengl32 note above).
    target_compile_definitions(Snapmaker_Orca_app_gui PRIVATE -DSLIC3R_WRAPPER_NOCONSOLE)
    target_link_libraries(Snapmaker_Orca_app_gui PRIVATE app_common)
    add_dependencies(Snapmaker_Orca_app_gui Snapmaker_Orca)
    set_target_properties(Snapmaker_Orca_app_gui PROPERTIES OUTPUT_NAME "snapmaker-orca")

    if (SLIC3R_SENTRY)
        target_link_libraries(Snapmaker_Orca_app_gui PRIVATE boost_headeronly sentry::sentry nlohmann_json)
    else()
        target_link_libraries(Snapmaker_Orca_app_gui PRIVATE boost_headeronly nlohmann_json)
    endif()
    if (SLIC3R_SENTRY)
        target_compile_definitions(Snapmaker_Orca_app_gui PRIVATE SLIC3R_SENTRY)
    endif()
endif ()

# Link the resources dir to where Slic3r GUI expects it
set(output_dlls_Release "")
set(output_dlls_Debug "")
set(output_dlls_RelWithDebInfo "")
# ---- Bundled Python/uv layout (canonical map) ---------------------------
# The runtime and uv are staged three ways because packaging differs:
#   install() rules          - Windows installer; on FHS/Flatpak they stage
#                              only uv (Flatpak's runtime comes from the deps
#                              build installing to /app/libpython; plain FHS
#                              installs bundle no python runtime)
#   POST_BUILD copies (here) - build-tree runs, and the macOS .app/AppImage
#                              flows, which package the build tree and
#                              never run install()
#   packaging scripts        - build_release_macos.sh (copies the .app),
#                              build_linux_image.sh.in (assembles $APPDIR)
# Final locations relative to the executable:
#   Windows:  <exe dir>/python            <resources>/tools/uv
#   macOS:    Contents/MacOS/python       Contents/MacOS/tools/uv
#   AppImage: $APPDIR/lib/python          $APPDIR/resources/tools/uv
#   Flatpak:  /app/libpython (dep prefix) /app/share/Snapmaker_Orca/tools/uv
# Runtime lookup: PythonInterpreter::initialize() / bundled_uv_path().
# libpython resolution: rpath on the Snapmaker_Orca target (above); the dep
# recipe gives the bundled interpreter a self-relative rpath.
# --------------------------------------------------------------------------
if (WIN32)
    # This has to be a separate target due to the windows command line length limits
    add_custom_target(COPY_DLLS ALL DEPENDS Snapmaker_Orca)

    if (CMAKE_CONFIGURATION_TYPES)
        foreach (CONF ${CMAKE_CONFIGURATION_TYPES})
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${CONF}" WIN_CONF_OUTPUT_DIR)
            file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/${CONF}/resources" WIN_RESOURCES_SYMLINK)
            add_custom_command(TARGET Snapmaker_Orca POST_BUILD
                COMMAND if exist "${WIN_CONF_OUTPUT_DIR}" "("
                        if not exist "${WIN_RESOURCES_SYMLINK}" "("
                            mklink /J "${WIN_RESOURCES_SYMLINK}" "${SLIC3R_RESOURCES_DIR_WIN}"
                        ")"
                    ")"
                COMMENT "Symlinking the resources directory into the build tree"
                VERBATIM
            )
        endforeach ()
    else ()
        # Single-config generators (Ninja, Makefiles)
        file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/resources" WIN_RESOURCES_SYMLINK)
        add_custom_command(TARGET Snapmaker_Orca POST_BUILD
            COMMAND if not exist "${WIN_RESOURCES_SYMLINK}" "(" mklink /J "${WIN_RESOURCES_SYMLINK}" "${SLIC3R_RESOURCES_DIR_WIN}" ")"
            COMMENT "Symlinking the resources directory into the build tree"
            VERBATIM
        )
    endif ()
    # copy libpython to the bin folder for Windows
    add_custom_command(TARGET Snapmaker_Orca POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:Snapmaker_Orca>/python"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_PREFIX_PATH}/libpython" "$<TARGET_FILE_DIR:Snapmaker_Orca>/python"
        COMMENT "Copying Python runtime into the build tree"
        VERBATIM)

    if (CMAKE_CONFIGURATION_TYPES)
        # Multi-config generators (Visual Studio, Ninja Multi-Config): copy per config.
        foreach (cfg ${CMAKE_CONFIGURATION_TYPES})
            if ("${cfg}" STREQUAL "Debug")
                Snapmaker_Orca_copy_dlls(COPY_DLLS "Debug" "d" output_dlls_Debug)
            elseif("${cfg}" STREQUAL "RelWithDebInfo")
                Snapmaker_Orca_copy_dlls(COPY_DLLS "RelWithDebInfo" "" output_dlls_RelWithDebInfo)
            else()
                Snapmaker_Orca_copy_dlls(COPY_DLLS "${cfg}" "" output_dlls_${cfg})
            endif()
        endforeach()
    else()
        # Single-config generators (Ninja): use CMAKE_BUILD_TYPE.
        if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
            Snapmaker_Orca_copy_dlls(COPY_DLLS "Debug" "d" output_dlls_Debug)
        elseif("${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
            Snapmaker_Orca_copy_dlls(COPY_DLLS "RelWithDebInfo" "" output_dlls_RelWithDebInfo)
        else()
            Snapmaker_Orca_copy_dlls(COPY_DLLS "Release" "" output_dlls_Release)
        endif()
    endif()

else ()
    if (NOT APPLE)
        # FFmpeg shared libs used by the media player, staged next to the binary.
        set(output_sos_Release "")
        set(output_sos_Debug "")
        set(output_sos_RelWithDebInfo "")
        add_custom_target(Snapmaker_OrcaSosCopy ALL DEPENDS Snapmaker_Orca)
        if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
            orcaslicer_copy_sos(Snapmaker_OrcaSosCopy "Debug" "d" output_sos_Debug)
        elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
            orcaslicer_copy_sos(Snapmaker_OrcaSosCopy "RelWithDebInfo" "" output_sos_RelWithDebInfo)
        else()
            orcaslicer_copy_sos(Snapmaker_OrcaSosCopy "Release" "" output_sos_Release)
        endif()
    endif()
    if (APPLE AND NOT CMAKE_MACOSX_BUNDLE)
        # On OSX, the name of the binary matches the name of the Application.
        add_custom_command(TARGET Snapmaker_Orca POST_BUILD
            COMMAND ln -sf Snapmaker_Orca snapmaker-orca
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:Snapmaker_Orca>"
            VERBATIM)
        # Copy Sentry crashpad_handler to executable directory for non-bundle builds (e.g., Xcode debugging)
        if (SLIC3R_SENTRY)
            add_custom_command(TARGET Snapmaker_Orca POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_PREFIX_PATH}/bin/crashpad_handler" "$<TARGET_FILE_DIR:Snapmaker_Orca>/crashpad_handler"
                COMMAND sh -c "codesign --force --sign - '$<TARGET_FILE_DIR:Snapmaker_Orca>/crashpad_handler' 2>/dev/null || true"
                COMMENT "Copying crashpad_handler for Sentry crash reporting"
                VERBATIM)
            # Copy libsentry.dylib to executable directory for non-bundle builds
            add_custom_command(TARGET Snapmaker_Orca POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_PREFIX_PATH}/lib/libsentry.dylib" "$<TARGET_FILE_DIR:Snapmaker_Orca>/libsentry.dylib"
                COMMAND sh -c "codesign --force --sign - '$<TARGET_FILE_DIR:Snapmaker_Orca>/libsentry.dylib' 2>/dev/null || true"
                COMMENT "Copying libsentry.dylib for Sentry crash reporting"
                VERBATIM)
        endif()
    else ()
        add_custom_command(TARGET Snapmaker_Orca POST_BUILD
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:Snapmaker_Orca>"
            VERBATIM)
    endif ()
    if (XCODE)
        # Because of Debug/Release/etc. configurations (similar to MSVC) the slic3r binary is located in an extra level
        set(BIN_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/resources")
        set(BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}")
        set(BIN_CONF_DIR "Debug")
    else ()
        set(BIN_RESOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/../resources")
        set(BIN_DIR "$<TARGET_FILE_DIR:Snapmaker_Orca>")
    endif ()
    if (CMAKE_MACOSX_BUNDLE)
        set(MACOSX_BUNDLE_ICON_FILE Icon.icns)
        set_target_properties(Snapmaker_Orca PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "Snapmaker Orca"
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${Snapmaker_VERSION}
            MACOSX_BUNDLE_COPYRIGHT "Copyright © 2024-2026 Snapmaker. All rights reserved."
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/modules/MacOSXBundleInfo.plist.in")
        # Get the bundle directory by going up from the executable to the .app bundle.
        # Derived from the target file dir rather than a hardcoded "<name>.app" path:
        # the bundle is "Snapmaker Orca.app" (with a space) while the executable is
        # Snapmaker_Orca, and this form works for both single- and multi-config generators.
        set(BIN_RESOURCES_DIR "$<TARGET_FILE_DIR:Snapmaker_Orca>/../Resources")
        set(BIN_DIR "$<TARGET_FILE_DIR:Snapmaker_Orca>")
        if (XCODE)
            # Xcode's CodeSign phase fails on the bundled Python runtime's dotted
            # dirs (python3.12) under Contents/MacOS. Skip it for local dev builds
            # and let the linker ad-hoc sign; the shipped bundle is signed by the
            # Ninja/CI packaging path (build_release_macos.sh, build_orca.yml).
            set_target_properties(Snapmaker_Orca PROPERTIES XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
        endif()
        # Create symlink for macOS bundle Resources directory
        # Remove existing Resources if it exists (could be from previous build), then create symlink
        add_custom_command(TARGET Snapmaker_Orca POST_BUILD
            COMMAND sh -c "rm -rf \"$<TARGET_FILE_DIR:Snapmaker_Orca>/../Resources\" || true"
            COMMAND ${CMAKE_COMMAND} -E create_symlink "${SLIC3R_RESOURCES_DIR}" "$<TARGET_FILE_DIR:Snapmaker_Orca>/../Resources"
            COMMENT "Symlinking the resources directory into the build tree"
            VERBATIM)
        # Copy Sentry crashpad_handler to MacOS directory (same directory as executable)
        if (SLIC3R_SENTRY)
            add_custom_command(TARGET Snapmaker_Orca POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_PREFIX_PATH}/bin/crashpad_handler" "$<TARGET_FILE_DIR:Snapmaker_Orca>/crashpad_handler"
                COMMAND sh -c "codesign --force --sign - '$<TARGET_FILE_DIR:Snapmaker_Orca>/crashpad_handler' 2>/dev/null || true"
                COMMENT "Copying and signing crashpad_handler for Sentry crash reporting"
                VERBATIM)
            # Copy libsentry.dylib to Frameworks directory
            add_custom_command(TARGET Snapmaker_Orca POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Snapmaker_Orca>/../Frameworks"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_PREFIX_PATH}/lib/libsentry.dylib" "$<TARGET_FILE_DIR:Snapmaker_Orca>/../Frameworks/libsentry.dylib"
                COMMAND sh -c "codesign --force --sign - '$<TARGET_FILE_DIR:Snapmaker_Orca>/../Frameworks/libsentry.dylib' 2>/dev/null || true"
                COMMENT "Copying and signing libsentry.dylib for Sentry crash reporting"
                VERBATIM)
            # Update the rpath for libsentry.dylib
            add_custom_command(TARGET Snapmaker_Orca POST_BUILD
                COMMAND install_name_tool -change "@rpath/libsentry.dylib" "@executable_path/../Frameworks/libsentry.dylib" "$<TARGET_FILE:Snapmaker_Orca>" || true            
                COMMENT "Updating rpath for libsentry.dylib"
                VERBATIM)
        endif()
    else ()
        # Non-bundle builds (Linux, and macOS without CMAKE_MACOSX_BUNDLE): link the
        # resources dir next to the build tree the way upstream Orca does.
        add_custom_command(TARGET Snapmaker_Orca POST_BUILD
            COMMAND ln -sfn "${SLIC3R_RESOURCES_DIR}" "${BIN_RESOURCES_DIR}"
            COMMENT "Symlinking the resources directory into the build tree"
            VERBATIM)
    endif()
    add_custom_command(TARGET Snapmaker_Orca POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${BIN_DIR}/python"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_PREFIX_PATH}/libpython" "${BIN_DIR}/python"
        COMMENT "Copying Python runtime into the build tree"
        VERBATIM)
    # Stage uv next to the binary for the build-tree/.app flows, which never
    # run install() -- see the layout map above; lookup is bundled_uv_path().
    if(ORCA_BUNDLED_UV_EXECUTABLE AND EXISTS "${ORCA_BUNDLED_UV_EXECUTABLE}")
        add_custom_command(TARGET Snapmaker_Orca POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${BIN_DIR}/tools/uv"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORCA_BUNDLED_UV_EXECUTABLE}" "${BIN_DIR}/tools/uv/${ORCA_BUNDLED_UV_FILENAME}"
            COMMAND chmod +x "${BIN_DIR}/tools/uv/${ORCA_BUNDLED_UV_FILENAME}"
            COMMENT "Copying bundled uv into the build tree"
            VERBATIM)
    endif()
endif ()

# Slic3r binary install target. Default build type is release in case no CMAKE_BUILD_TYPE is provided.
if( ("${CMAKE_BUILD_TYPE}" STREQUAL "Release") OR ("${CMAKE_BUILD_TYPE}" STREQUAL "") )
    set (build_type "Release")
    set(CMAKE_BUILD_POSTFIX "")
elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
    set (build_type "RelWithDebInfo")
    set(CMAKE_BUILD_POSTFIX "")
else()
    set (build_type "Debug")
    set(CMAKE_BUILD_POSTFIX "d")
endif()
message(STATUS "libslic3r-CMAKE_BUILD_TYPE: ${build_type}")
message(STATUS "CMAKE_CURRENT_BINARY_DIR: ${CMAKE_CURRENT_BINARY_DIR}")
if (WIN32)
    install(TARGETS Snapmaker_Orca RUNTIME DESTINATION ".")
    if (MSVC)
        install(TARGETS Snapmaker_Orca_app_gui RUNTIME DESTINATION ".")
    endif ()
    install(FILES ${output_dlls_${build_type}} DESTINATION ".")
    install(DIRECTORY "${CMAKE_PREFIX_PATH}/libpython/" DESTINATION "python")
    # MESA llvmpipe software-OpenGL fallback: the snapmaker-orca.exe shim loads
    # <install>/mesa/opengl32.dll before Snapmaker_Orca.dll whenever the system
    # OpenGL driver is unusable (< 2.0, e.g. disabled GPU -> GDI generic 1.1),
    # see src/Snapmaker_Orca_app_msvc.cpp and deps/mesa/README.md.
    if (CMAKE_SYSTEM_PROCESSOR STREQUAL "ARM64")
        # The bundled MESA opengl32.dll is x64 only.
    elseif (EXISTS "${CMAKE_SOURCE_DIR}/deps/mesa/x64/opengl32.dll")
        install(FILES "${CMAKE_SOURCE_DIR}/deps/mesa/x64/opengl32.dll" DESTINATION "mesa")
    else ()
        message(WARNING "deps/mesa/x64/opengl32.dll not found: the MESA software OpenGL fallback will NOT be packaged (see deps/mesa/README.md).")
    endif ()
else ()
    if (NOT APPLE AND output_sos_${build_type})
        install(FILES ${output_sos_${build_type}} DESTINATION "${CMAKE_INSTALL_PREFIX}")
    endif()
    install(TARGETS Snapmaker_Orca RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR})
endif ()
