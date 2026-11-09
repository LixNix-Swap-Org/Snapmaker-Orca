get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests_main.cpp
    test_bambu_filament_ids.cpp
    test_bed_exclusion_fields.cpp
    test_cloud_delete_queue.cpp
    test_config_value_formatter.cpp
    test_creality_cfs_match.cpp
    test_creality_print.cpp
    test_dev_mapping.cpp
    test_device_access.cpp
    test_moonraker_printer_agent.cpp
    test_network_discovery.cpp
    test_filament_bitmap_utils.cpp
    test_colour_picker_field.cpp
    test_filament_color_picker.cpp
    test_ams_materials_setting.cpp
    test_gcode_preview_colors.cpp
    test_device_progress.cpp
    test_device_manager_integration.cpp
    test_web_media_controller.cpp
    test_scene_raycaster.cpp
    test_scene_benchmark.cpp
    test_lazy.cpp
    test_prebuild_queue.cpp
    test_staged_build.cpp
    test_network_versions.cpp
    test_orca_cloud_agent.cpp
    test_action_source.cpp
    test_sswcp_machine_info.cpp # Snapmaker Orca: device identity parsing
    test_high_flow_notices.cpp # Snapmaker Orca: High Flow allow list and tool head rules
    test_per_head_process_sites.cpp # Snapmaker Orca: every GUI Print::apply is fed by full_config_for_print
    test_mesh_lod_pool.cpp # Snapmaker Orca: render LOD worker pool and cache
    test_scene_cache.cpp # Snapmaker Orca: scene cache key inputs and the blit texture coordinate
    test_ota_package_digest.cpp # Snapmaker Orca: digest check of a downloaded OTA profile package
    test_ota_update_rules.cpp # Snapmaker Orca: GUI-free decisions of the OTA install path
    test_http_status.cpp # Snapmaker Orca: every final status outside 200-299 except 304 reaches on_error
    test_snaplog_wiring.cpp # Snapmaker Orca: transport guard, consent rule and text scrubbing around the SnapLog client
    test_timeout_map.cpp # Snapmaker Orca: expiry, pause and snapshot rules of the TimeoutMap behind the SSWCP instance list
    test_option_state_registry.cpp
    test_plugin_host_api.cpp
    # Exercise seam enums and predicates through the embedded Python host API.
    test_precise_seam_plugin.cpp
    test_plugin_capability_config.cpp
    test_plugin_config.cpp
    test_plugin_capabilities_in_use.cpp
    test_plugin_status.cpp
    test_printer_agent.cpp
    test_qidi_printer_agent.cpp
    test_plugin_install.cpp
    test_plugin_lifecycle.cpp
    test_plugin_resource_texture.cpp
    test_plugin_printer_agent.cpp
    test_post_processor.cpp
    test_printhost.cpp
    test_slicing_pipeline_bindings.cpp
    test_slicing_pipeline_config.cpp
    test_plugin_sort.cpp
    test_plugin_cloud_metadata.cpp
    test_plugin_audit.cpp
    SnapLogClient_test.cpp # Snapmaker Orca: verbatim from Snapmaker upstream b1831e5dcb, tag [snaplog]
    FilamentDropDown_test.cpp
    FilamentSort_test.cpp
    test_plugin_json_depth.cpp
    test_shortcuts.cpp
    test_texture_color_palette.cpp
    test_texture_island_view.cpp
    test_timelapse_codecs.cpp
    test_file_url.cpp
    test_imgui_wrapper.cpp
    test_user_manager.cpp
    test_sequential_clearance.cpp
    test_object_list.cpp
    ../fff_print/test_helpers.cpp
    )

if (SLIC3R_CAD)
    target_sources(${_TEST_NAME}_tests PRIVATE test_design_sketch_tool.cpp)
endif ()

if (MSVC)
    target_link_libraries(${_TEST_NAME}_tests Setupapi.lib)
endif ()
target_compile_definitions(${_TEST_NAME}_tests PRIVATE
    "FILAMENT_ALLOW_LIST_FILE=R\"(${CMAKE_SOURCE_DIR}/resources/profiles/Snapmaker/filament/filament_allow_list.json)\"")

# Snapmaker Orca: the upstream test file is a byte-identical copy and is never edited here, so its
# warnings stay warnings instead of failing the -Werror build.
if ((NOT MSVC OR IS_CLANG_CL) AND ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang"))
    set_source_files_properties(SnapLogClient_test.cpp PROPERTIES COMPILE_OPTIONS "-Wno-error")
endif ()

target_link_libraries(${_TEST_NAME}_tests test_common libslic3r_gui libslic3r pybind11::embed Catch2::Catch2WithMain)
if (SLIC3R_PCH AND NOT SLIC3R_SYNTAXONLY)
    add_precompiled_header(${_TEST_NAME}_tests ${CMAKE_SOURCE_DIR}/src/slic3r/pchheader.hpp FORCEINCLUDE)
endif ()

set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

orcaslicer_copy_test_dlls(${_TEST_NAME}_tests)

add_executable(gateway_tests
    ConnectionProcessManager_test.cpp
    GatewayProtocol_test.cpp
    GatewayService_test.cpp
    GatewayDevice_test.cpp
    GatewayAccount_test.cpp
    gateway_nanosvg.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/slic3r/Utils/ConnectionProcessManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/slic3r/Utils/GatewayProtocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/slic3r/Utils/GatewayService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/slic3r/Utils/GatewayDevice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/slic3r/Utils/GatewayAccount.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/slic3r/Utils/Http.cpp
    )
target_link_libraries(gateway_tests test_common libslic3r boost_libs libcurl OpenSSL::SSL OpenSSL::Crypto Catch2::Catch2WithMain app_common)
if (MSVC)
    target_compile_definitions(gateway_tests PRIVATE _WIN32_WINNT=0x0601 NOMINMAX)
endif ()
set_property(TARGET gateway_tests PROPERTY FOLDER "tests")

if (WIN32)
    add_custom_command(TARGET ${_TEST_NAME}_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${_TEST_NAME}_tests>/python"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_PREFIX_PATH}/libpython" "$<TARGET_FILE_DIR:${_TEST_NAME}_tests>/python"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_PREFIX_PATH}/libpython/python${_bundled_python_abi}.dll"
            "${CMAKE_PREFIX_PATH}/libpython/vcruntime140.dll"
            "${CMAKE_PREFIX_PATH}/libpython/vcruntime140_1.dll"
            "$<TARGET_FILE_DIR:${_TEST_NAME}_tests>"
        COMMENT "Copying Python runtime for slic3rutils plugin host API tests"
        VERBATIM
    )
elseif (NOT FLATPAK)
    # The CI unit-test runner only receives the build/tests tree, so both the
    # interpreter and the libpython the test binary links have to travel next to
    # the executable; find_bundled_python_home() picks the copy up from there.
    if (APPLE)
        target_link_options(${_TEST_NAME}_tests PRIVATE
            "LINKER:-rpath,@executable_path/python/lib")
    else ()
        set_property(TARGET ${_TEST_NAME}_tests APPEND PROPERTY BUILD_RPATH "$ORIGIN/python/lib")
    endif ()

    add_custom_command(TARGET ${_TEST_NAME}_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_FILE_DIR:${_TEST_NAME}_tests>/python"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_PREFIX_PATH}/libpython"
            "$<TARGET_FILE_DIR:${_TEST_NAME}_tests>/python"
        COMMENT "Copying Python runtime for the plugin host API tests"
        VERBATIM
    )
elseif (FLATPAK)
    # Same <testdir>/python home as WIN32/APPLE; symlink since /app/libpython
    # already ships in the flatpak (the test exe links libpython3.12.so from it).
    add_custom_command(TARGET ${_TEST_NAME}_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "$<TARGET_FILE_DIR:${_TEST_NAME}_tests>/python"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${CMAKE_PREFIX_PATH}/libpython"
            "$<TARGET_FILE_DIR:${_TEST_NAME}_tests>/python"
        COMMENT "Linking Python runtime for flatpak plugin host API tests"
        VERBATIM
    )
endif()

# scripts/run_unit_tests.sh installs the tests' numpy into that runtime with this uv,
# staged where the app build tree keeps it (<exe dir>/tools/uv, see src/CMakeLists.txt).
if (ORCA_BUNDLED_UV_EXECUTABLE)
    add_custom_command(TARGET ${_TEST_NAME}_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${_TEST_NAME}_tests>/tools/uv"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORCA_BUNDLED_UV_EXECUTABLE}"
            "$<TARGET_FILE_DIR:${_TEST_NAME}_tests>/tools/uv/${ORCA_BUNDLED_UV_FILENAME}"
        COMMENT "Copying uv for the plugin host API tests"
        VERBATIM
    )
endif()

# Snapmaker Orca: the "[pipeline]" cases of SnapLogClient_test.cpp race the realtime worker that
# init() starts (it may pop one queued event), so they carry the NotWorking label and leave the
# default ctest run; run them with slic3rutils_tests "[pipeline]" or ctest -L NotWorking.
catch_discover_tests(${_TEST_NAME}_tests TEST_SPEC "~[pipeline]" ADD_TAGS_AS_LABELS)
catch_discover_tests(${_TEST_NAME}_tests TEST_SPEC "[pipeline]" PROPERTIES LABELS NotWorking)

orcaslicer_discover_tests(gateway_tests)
