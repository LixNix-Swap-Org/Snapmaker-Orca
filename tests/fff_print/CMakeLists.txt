get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests
	${_TEST_NAME}_tests.cpp
	test_helpers.cpp
	test_helpers.hpp
	test_adaptive_pressure_advance.cpp
	test_calib.cpp
	test_cooling.cpp
	test_extrusion_entity.cpp
	test_extrusion_processor.cpp
	test_exclusion_volumes.cpp
	test_fan_mover.cpp
	test_fill.cpp
	test_flow.cpp
	test_gcode_processor.cpp
	test_gcode_statistics.cpp
	test_gcode_timing.cpp
	test_gcode.cpp
	test_gcodeprocessor.cpp
	test_gcodewriter.cpp
	test_imex_mode_gcode.cpp
	test_imex_slice_offset.cpp
	test_model.cpp
	test_multifilament.cpp
	test_multi_nozzle_layer_height.cpp
	test_high_flow.cpp # Snapmaker Orca: High Flow columns per tool head
	test_nozzle_filament.cpp # Snapmaker Orca: filament presets per nozzle size of the tool head
	test_per_head_process.cpp # Snapmaker Orca: process speeds per tool head, what only a slice shows
	test_painted_segmentation.cpp # Snapmaker Orca: painted segmentation with outer-wall overrides and layer runs
	test_multimaterial_segmentation.cpp
	test_perimeters.cpp
	# Seam geometry and pipeline regressions share the existing Print fixtures.
	test_precise_seam.cpp
	test_print.cpp
	test_printobject.cpp
	test_mixed_filament.cpp
	test_seam_placer.cpp
	test_skirt_brim.cpp
	test_slicing_pipeline_hook.cpp
	test_spiral_vase.cpp
	test_support_material.cpp
	test_tree_support_bridge.cpp
	test_bed_temperature.cpp
	test_texture_displacement.cpp
	test_tree_support.cpp
	test_trianglemesh.cpp
	test_wipe.cpp
	test_pressure_equalizer.cpp
	test_fanmover.cpp
	test_small_area_infill_flow_compensator.cpp
	test_cooling_buffer.cpp
	test_adaptive_pa.cpp
	test_wipe_tower.cpp
	)
# libslic3r_cgal links CGAL privately, so the GMP/MPFR libraries are needed at link time.
# They are not propagated to this test target; find them under the deps prefix or the
# system paths, and only add them when found to avoid linking a NOTFOUND placeholder.
find_library(GMP_LIB  NAMES libgmp-10  gmp  PATHS "${CMAKE_PREFIX_PATH}/lib")
find_library(MPFR_LIB NAMES libmpfr-4 mpfr PATHS "${CMAKE_PREFIX_PATH}/lib")
if (GMP_LIB AND MPFR_LIB)
    target_link_libraries(${_TEST_NAME}_tests test_common libslic3r Catch2::Catch2WithMain OpenSSL::Crypto ${GMP_LIB} ${MPFR_LIB})
else()
    target_link_libraries(${_TEST_NAME}_tests test_common libslic3r Catch2::Catch2WithMain OpenSSL::Crypto)
endif()
if (WIN32)
    target_link_libraries(${_TEST_NAME}_tests bcrypt.lib)
endif()
if (SLIC3R_PCH AND NOT SLIC3R_SYNTAXONLY)
    add_precompiled_header(${_TEST_NAME}_tests ${CMAKE_SOURCE_DIR}/src/libslic3r/pchheader.hpp FORCEINCLUDE)
endif ()

set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

orcaslicer_copy_test_dlls()

orcaslicer_discover_tests(${_TEST_NAME}_tests)
