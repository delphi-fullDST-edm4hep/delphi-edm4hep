# Tests for delphi_edm4hep. A full conversion test suite over real bank data
# is future work.
#
# Each pass must print its usage message when run with no arguments. Matching
# the usage text rather than just a nonzero exit also proves the binary loaded
# its shared libraries and reached main(). No data files are needed.
add_test(NAME cli_sdst_rejects_no_args COMMAND delphi_sdst_pass)
add_test(NAME cli_fdst_rejects_no_args COMMAND delphi_fdst_pass)
add_test(NAME cli_btag_check_rejects_no_args COMMAND delphi_btag_check)
set_tests_properties(cli_sdst_rejects_no_args
  PROPERTIES PASS_REGULAR_EXPRESSION "usage: .*input\\.sdst")
set_tests_properties(cli_fdst_rejects_no_args
  PROPERTIES PASS_REGULAR_EXPRESSION "usage: .*input\\.fadana")
set_tests_properties(cli_btag_check_rejects_no_args
  PROPERTIES PASS_REGULAR_EXPRESSION "usage: .*input\\.edm4hep\\.root")
add_test(NAME cli_btag_check_rejects_invalid_source
         COMMAND delphi_btag_check --source wrong missing.root data)
set_tests_properties(cli_btag_check_rejects_invalid_source
  PROPERTIES PASS_REGULAR_EXPRESSION "--source must be sDST or fDST")

# Pure regression guard for PSFBTG's IERRBS bypass. In that branch AABTGS is
# not called and IBAD is stale, so raw status and current-event validity must
# remain separate concepts.
add_executable(aabtag_status_test aabtag_status_test.cpp)
target_include_directories(aabtag_status_test PRIVATE ${PROJECT_SOURCE_DIR}/include)
add_test(NAME aabtag_status_contract COMMAND aabtag_status_test)

add_executable(btag_check_domains_test btag_check_domains_test.cpp)
target_include_directories(btag_check_domains_test
  PRIVATE ${PROJECT_SOURCE_DIR}/include)
add_test(NAME btag_check_domain_contract COMMAND btag_check_domains_test)

add_executable(particle_counts_test particle_counts_test.cpp)
target_include_directories(particle_counts_test
  PRIVATE ${PROJECT_SOURCE_DIR}/include)
add_test(NAME native_particle_counts_contract COMMAND particle_counts_test)

add_executable(cargo_database_test cargo_database_test.cpp)
target_link_libraries(cargo_database_test PRIVATE delphi_geometry)
add_test(NAME cargo_database_contract COMMAND cargo_database_test)

add_executable(geometry_model_test geometry_model_test.cpp)
target_link_libraries(geometry_model_test PRIVATE delphi_geometry)
add_test(NAME geometry_model_contract COMMAND geometry_model_test)

add_executable(gdml_world_writer_test gdml_world_writer_test.cpp)
target_link_libraries(gdml_world_writer_test PRIVATE delphi_geometry)
add_test(NAME gdml_world_writer_contract COMMAND gdml_world_writer_test)

add_executable(gdml_beam_pipe_writer_test gdml_beam_pipe_writer_test.cpp)
target_link_libraries(gdml_beam_pipe_writer_test PRIVATE delphi_geometry)
add_test(NAME gdml_beam_pipe_writer_contract COMMAND gdml_beam_pipe_writer_test)

add_executable(tpc_readout_geometry_test tpc_readout_geometry_test.cpp)
target_link_libraries(tpc_readout_geometry_test PRIVATE delphi_geometry)
add_test(NAME tpc_readout_geometry_contract COMMAND tpc_readout_geometry_test)

add_executable(tpc_pad_response_test tpc_pad_response_test.cpp)
target_link_libraries(tpc_pad_response_test PRIVATE delphi_geometry)
add_test(NAME tpc_pad_response_contract COMMAND tpc_pad_response_test)

add_executable(tpc_digitization_conditions_test
               tpc_digitization_conditions_test.cpp)
target_link_libraries(tpc_digitization_conditions_test PRIVATE delphi_geometry)
add_test(NAME tpc_digitization_conditions_contract
         COMMAND tpc_digitization_conditions_test)

add_executable(tpc_time_response_test tpc_time_response_test.cpp)
target_link_libraries(tpc_time_response_test PRIVATE delphi_geometry)
add_test(NAME tpc_time_response_contract COMMAND tpc_time_response_test)

add_executable(tpc_fadc_test tpc_fadc_test.cpp)
target_link_libraries(tpc_fadc_test PRIVATE delphi_geometry)
add_test(NAME tpc_fadc_contract COMMAND tpc_fadc_test)

add_executable(tpc_wire_geometry_test tpc_wire_geometry_test.cpp)
target_link_libraries(tpc_wire_geometry_test PRIVATE delphi_geometry)
add_test(NAME tpc_wire_geometry_contract COMMAND tpc_wire_geometry_test)

add_executable(tpc_wire_response_test tpc_wire_response_test.cpp)
target_link_libraries(tpc_wire_response_test PRIVATE delphi_geometry)
add_test(NAME tpc_wire_response_contract COMMAND tpc_wire_response_test)

add_executable(inner_detector_jet_response_test
               inner_detector_jet_response_test.cpp)
target_link_libraries(inner_detector_jet_response_test PRIVATE delphi_geometry)
add_test(NAME inner_detector_jet_response_contract
         COMMAND inner_detector_jet_response_test)

add_executable(outer_detector_response_test outer_detector_response_test.cpp)
target_link_libraries(outer_detector_response_test PRIVATE delphi_geometry)
add_test(NAME outer_detector_response_contract COMMAND outer_detector_response_test)

add_executable(central_track_fit_test central_track_fit_test.cpp)
target_link_libraries(central_track_fit_test PRIVATE delphi_geometry)
add_test(NAME central_track_fit_contract COMMAND central_track_fit_test)

add_executable(central_track_finder_test central_track_finder_test.cpp)
target_link_libraries(central_track_finder_test PRIVATE delphi_geometry)
add_test(NAME central_track_finder_contract COMMAND central_track_finder_test)

add_executable(helix_trajectory_test helix_trajectory_test.cpp)
target_link_libraries(helix_trajectory_test PRIVATE delphi_geometry)
add_test(NAME helix_trajectory_contract COMMAND helix_trajectory_test)

add_executable(vertex_digitization_conditions_test
               vertex_digitization_conditions_test.cpp)
target_link_libraries(vertex_digitization_conditions_test PRIVATE delphi_geometry)
add_test(NAME vertex_digitization_conditions_contract
         COMMAND vertex_digitization_conditions_test)

add_executable(vertex_channel_response_test vertex_channel_response_test.cpp)
target_link_libraries(vertex_channel_response_test PRIVATE delphi_geometry)
add_test(NAME vertex_channel_response_contract COMMAND vertex_channel_response_test)

# PHDST-sample-backed fault injection for the C++/Fortran callback boundary.
# The runner skips when DELPHI_PHDST_SAMPLE is not set, like alignment_audit.
add_executable(harness_exception_probe harness_exception_probe.cpp)
target_link_libraries(harness_exception_probe
  PRIVATE delphi_edm4hep delphi_fortran_link)

# Alignment / relation audit: regression guard for the bug class where a
# UserData array is mislabelled as parallel to the wrong collection (e.g.
# per-particle vs per-track), or a relation (RecDqdx -> Track) is left unset.
# Runs align_audit.py against the EDM4hep file named in DELPHI_EDM4HEP_SAMPLE;
# with no sample set the script exits 77 (CTest skip), so this is a no-op until
# CI points it at a converted file. Needs Python3 + podio (the key4hep view).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
  add_test(NAME alignment_audit
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/align_audit.py)
  set_tests_properties(alignment_audit PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME btag_prefix_integration
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/btag_prefix_integration.py
                   $<TARGET_FILE:delphi_btag_check>)
  set_tests_properties(btag_prefix_integration PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME harness_exception_containment
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/harness_exception_containment.py
                   $<TARGET_FILE:harness_exception_probe>)
  set_tests_properties(harness_exception_containment
    PROPERTIES SKIP_RETURN_CODE 77)
endif()
