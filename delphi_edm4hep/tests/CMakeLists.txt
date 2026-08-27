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
         COMMAND delphi_btag_check --source wrong missing.root data recalc)
set_tests_properties(cli_btag_check_rejects_invalid_source
  PROPERTIES PASS_REGULAR_EXPRESSION "--source must be sDST or fDST")
add_test(NAME cli_btag_check_rejects_invalid_primary_vertex_policy
         COMMAND delphi_btag_check --source sDST
                 --primary-vertex-policy wrong missing.root data recalc)
set_tests_properties(cli_btag_check_rejects_invalid_primary_vertex_policy
  PROPERTIES PASS_REGULAR_EXPRESSION
             "--primary-vertex-policy must be keep-delana or replace-with-aabtag")
add_test(NAME cli_btag_check_rejects_missing_primary_vertex_policy
         COMMAND delphi_btag_check --source sDST --primary-vertex-policy)
set_tests_properties(cli_btag_check_rejects_missing_primary_vertex_policy
  PROPERTIES PASS_REGULAR_EXPRESSION
             "--primary-vertex-policy requires keep-delana or replace-with-aabtag")
add_test(NAME cli_btag_check_rejects_fdst_identity_reference
         COMMAND delphi_btag_check --source fDST missing.root data recalc reference.root)
set_tests_properties(cli_btag_check_rejects_fdst_identity_reference
  PROPERTIES PASS_REGULAR_EXPRESSION "identity reference validates copied sDST_EVT")
add_test(NAME cli_btag_check_rejects_sdst_legacy_fallback
         COMMAND delphi_btag_check --source sDST --legacy-fdst-beamspot-fallback
                 missing.root data recalc)
set_tests_properties(cli_btag_check_rejects_sdst_legacy_fallback
  PROPERTIES PASS_REGULAR_EXPRESSION "valid only with --source fDST")
add_test(NAME cli_btag_check_rejects_reference_fallback_without_reference
         COMMAND delphi_btag_check --source sDST
                 --legacy-reference-beamspot-fallback missing.root data recalc)
set_tests_properties(cli_btag_check_rejects_reference_fallback_without_reference
  PROPERTIES PASS_REGULAR_EXPRESSION "requires an identity-reference file")

# Pure regression guard for PSFBTG's IERRBS bypass. In that branch AABTGS is
# not called and IBAD is stale, so raw status and current-event validity must
# remain separate concepts.
add_executable(aabtag_status_test aabtag_status_test.cpp)
target_include_directories(aabtag_status_test PRIVATE ${PROJECT_SOURCE_DIR}/include)
add_test(NAME aabtag_status_contract COMMAND aabtag_status_test)

add_executable(btag_provenance_test btag_provenance_test.cpp)
target_include_directories(btag_provenance_test PRIVATE ${PROJECT_SOURCE_DIR}/include)
add_test(NAME btag_provenance_contract COMMAND btag_provenance_test)

add_executable(btag_check_domains_test btag_check_domains_test.cpp)
target_include_directories(btag_check_domains_test
  PRIVATE ${PROJECT_SOURCE_DIR}/include)
add_test(NAME btag_check_domain_contract COMMAND btag_check_domains_test)

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
  add_test(NAME btag_checker_content_integration
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/btag_checker_content_integration.py
                   $<TARGET_FILE:delphi_btag_check>)
  set_tests_properties(btag_checker_content_integration
    PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME harness_exception_containment
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/harness_exception_containment.py
                   $<TARGET_FILE:harness_exception_probe>)
  set_tests_properties(harness_exception_containment
    PROPERTIES SKIP_RETURN_CODE 77)
endif()
