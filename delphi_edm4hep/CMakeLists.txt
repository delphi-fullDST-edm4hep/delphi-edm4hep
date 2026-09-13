cmake_minimum_required(VERSION 3.24)  # LINK_GROUP genex needs >=3.24
project(delphi_edm4hep
  LANGUAGES CXX
  DESCRIPTION "DELPHI ZEBRA -> EDM4hep converter (per-domain library, two-pass)"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Build type default: Release with debug info.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra)

# A key4hep stack must be sourced before configuring. setup.sh sets PATH,
# LD_LIBRARY_PATH, CMAKE_PREFIX_PATH and the compiler, and those must stay
# live through build and ctest — CMake cannot do it for you. Configuring
# without a stack silently picks up whatever EDM4hep/podio/ROOT happen to be
# visible, and that mismatch only shows up at runtime. Fail here instead.
#
# This file deliberately does NOT name a stack release: choosing a stack is
# the caller's job. It declares the API floors the source code needs and
# reports what the sourced stack provided. See .github/workflows/ for the
# release channels CI exercises.
option(KEY4HEP_STACK_REQUIRED
  "Fail configure when no key4hep stack has been sourced" ON)

if(KEY4HEP_STACK_REQUIRED AND NOT DEFINED ENV{KEY4HEP_STACK})
  # The release CI treats as production. Read only to make this message
  # actionable — nothing in the build depends on its value.
  file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../.github/key4hep-production-release"
    _k4h_prod LIMIT_COUNT 1)
  string(STRIP "${_k4h_prod}" _k4h_prod)
  message(FATAL_ERROR
    "key4hep: no stack sourced (\$KEY4HEP_STACK is unset).\n"
    "Set up the environment first:\n"
    "  source /cvmfs/delphi.cern.ch/setup.sh\n"
    "  source /cvmfs/sw.hsf.org/key4hep/setup.sh -r ${_k4h_prod}\n"
    "  unset CXXFLAGS CFLAGS LDFLAGS\n"
    "then configure into a clean build dir.\n"
    "${_k4h_prod} is the production release, recorded in "
    ".github/key4hep-production-release. Any stack meeting the version floors "
    "works: drop '-r' for the current stable release, or source "
    "/cvmfs/sw-nightlies.hsf.org/key4hep/setup.sh for a nightly.\n"
    "To build against an EDM4hep/podio provided some other way, pass "
    "-DKEY4HEP_STACK_REQUIRED=OFF.")
endif()

if(DEFINED ENV{KEY4HEP_STACK})
  message(STATUS "key4hep stack: $ENV{KEY4HEP_STACK}")
else()
  message(STATUS "key4hep stack: none sourced (KEY4HEP_STACK_REQUIRED=OFF)")
endif()

find_package(EDM4HEP 1.0 REQUIRED)
find_package(podio   1.7 REQUIRED)
find_package(ROOT        REQUIRED COMPONENTS Core RIO Matrix)
message(STATUS
  "key4hep: EDM4hep ${EDM4HEP_VERSION}, podio ${podio_VERSION}, ROOT ${ROOT_VERSION}")

# Native, dependency-free reader for the authoritative DELPHI CARGO/DDAPP
# detector snapshot. Geometry conversion is built on this library rather than
# linking the legacy database runtime into Code4hep.
add_library(delphi_geometry STATIC
  src/Reconstruction/CentralTrackFinder.cpp
  src/Reconstruction/CentralTrackFit.cpp
  src/Reconstruction/HelixTrajectory.cpp
  src/Geometry/CargoDatabase.cpp
  src/Geometry/GdmlBeamPipeWriter.cpp
  src/Geometry/GdmlWorldWriter.cpp
  src/Geometry/GeometryModel.cpp
  src/Simulation/InnerDetectorJetResponse.cpp
  src/Simulation/InnerDetectorReadoutGeometry.cpp
  src/Simulation/OuterDetectorDriftResponse.cpp
  src/Simulation/OuterDetectorPhysicalChannelPayload.cpp
  src/Simulation/OuterDetectorReadoutGeometry.cpp
  src/Simulation/TpcDigitizationConditions.cpp
  src/Simulation/TpcFadc.cpp
  src/Simulation/TpcPadResponse.cpp
  src/Simulation/TpcReadoutGeometry.cpp
  src/Simulation/TpcTimeResponse.cpp
  src/Simulation/TpcWireGeometry.cpp
  src/Simulation/TpcWireResponse.cpp
  src/Simulation/VertexChannelResponse.cpp
  src/Simulation/VertexDigitizationConditions.cpp
  src/Simulation/VertexReadoutGeometry.cpp
  src/Simulation/VertexStripReadout.cpp
)
target_include_directories(delphi_geometry
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

add_executable(delphi_geometry_export bin/delphi_geometry_export.cpp)
target_link_libraries(delphi_geometry_export PRIVATE delphi_geometry)

add_executable(delphi_tpc_readout_audit bin/delphi_tpc_readout_audit.cpp)
target_link_libraries(delphi_tpc_readout_audit PRIVATE delphi_geometry)

add_executable(delphi_id_readout_audit bin/delphi_id_readout_audit.cpp)
target_link_libraries(delphi_id_readout_audit PRIVATE delphi_geometry)

add_executable(delphi_od_readout_audit bin/delphi_od_readout_audit.cpp)
target_link_libraries(delphi_od_readout_audit PRIVATE delphi_geometry)

add_executable(delphi_vertex_readout_audit bin/delphi_vertex_readout_audit.cpp)
target_link_libraries(delphi_vertex_readout_audit PRIVATE delphi_geometry)

# DELPHI almalinux-9 Fortran libraries (custom find module).
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
find_package(DelphiAL9 REQUIRED)

# ---------------------------------------------------------------------------
# delphi_edm4hep library: C++ converter code, depending on edm4hep + podio +
# ROOT + selected DELPHI services. The Fortran archives are linked only by the
# binaries, using a --start-group linker group for circular dependencies.
# A Code4hep source is a shared plugin.  The legacy DELPHI and CERNLIB
# archives are not position independent, so they cannot be linked into that
# plugin.  In the embedded build this converter library is shared and leaves
# its Fortran references for the delphiRun executable to provide.
set(_delphi_library_type STATIC)
if(COMMAND c4h_add_plugin)
  set(_delphi_library_type SHARED)
endif()
add_library(delphi_edm4hep ${_delphi_library_type}
  src/AabtagTrackState.cpp
  src/BankPrefix.cpp
  src/DstCensus.cpp
  src/BankReader.cpp
  src/CollectionWriter.cpp
  src/ConversionPipeline.cpp
  src/PerigeeMatch.cpp
  src/Helix.cpp
  src/TeBank.cpp
  src/PhdstHarness.cpp
  src/Event/EventInfo.cpp
  src/Event/Event.cpp
  src/Vertex/Vertex.cpp
  src/Btag/BtagInfo.cpp
  src/Btag/Btag.cpp
  src/Tracking/TrackElements.cpp
  src/Tracking/Tracking.cpp
  src/Calorimeter/Calorimeter.cpp
  src/Pid/ParticleId.cpp
  src/Truth/Truth.cpp
  src/Tracking/MatchProvenance.cpp
  src/Pid/Tof.cpp
  src/Pid/Mtpc.cpp
  src/Tracking/TrackHybrid.cpp
  src/Calorimeter/Emca.cpp
  src/Calorimeter/HcalFdst.cpp
  src/Calorimeter/TeadFdst.cpp
  src/Tracking/MainHybrid.cpp
  src/Pid/PidHybrid.cpp
  src/Calorimeter/ShowerHybrid.cpp
  src/Truth/TblHybrid.cpp
  src/Pid/PaPidExtras.cpp
  src/Calorimeter/Tdha.cpp
  src/Pid/SdstPaExtras.cpp
  src/Calorimeter/SticShower.cpp
  src/Calorimeter/SticInfo.cpp
  src/Tracking/EltrSdst.cpp
  src/Tracking/Trax.cpp
  src/Tracking/VdHits.cpp
  src/Tracking/VftHits.cpp
  src/Pid/PidExtrasSdst.cpp
)
target_include_directories(delphi_edm4hep
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(delphi_edm4hep
  PUBLIC
    EDM4HEP::edm4hep
    podio::podio
    podio::podioRootIO
    ROOT::Core
    ROOT::RIO
    ROOT::Matrix
  PRIVATE
    DelphiAL9::headers
)

# ---------------------------------------------------------------------------
# Helper interface target: full DELPHI Fortran linker group for binaries.
# Pulls in everything in the --start-group --end-group block plus gfortran
# and the small C deps.
add_library(delphi_fortran_link INTERFACE)
target_link_libraries(delphi_fortran_link
  INTERFACE
    DelphiAL9::fortran_group
    gfortran dl m z crypt
)

add_library(delphi_fortran_no_skelana_link INTERFACE)
target_link_libraries(delphi_fortran_no_skelana_link
  INTERFACE
    DelphiAL9::fortran_group_no_skelana
    gfortran dl m z crypt
)

# ---------------------------------------------------------------------------
# Pass-1 binary (sDST -> intermediate edm4hep).
add_executable(delphi_sdst_pass bin/delphi_sdst_pass.cpp)
target_link_libraries(delphi_sdst_pass
  PRIVATE delphi_edm4hep delphi_fortran_no_skelana_link
)

# Pass-2 binary (intermediate edm4hep + fDST -> final edm4hep).
add_executable(delphi_fdst_pass bin/delphi_fdst_pass.cpp)
target_link_libraries(delphi_fdst_pass
  PRIVATE delphi_edm4hep delphi_fortran_no_skelana_link
)

# Native Code4hep/Stitched input source. It is built automatically when this
# project is embedded in Code4hep, and omitted from the standalone Key4hep
# build where the framework targets are unavailable.
set(_delphi_code4hep_default OFF)
if(COMMAND c4h_add_plugin AND TARGET IOUtilities)
  set(_delphi_code4hep_default ON)
endif()
option(DELPHI_BUILD_CODE4HEP_SOURCE
  "Build the native Code4hep DelphiSource plugin" ${_delphi_code4hep_default})
if(DELPHI_BUILD_CODE4HEP_SOURCE)
  if(NOT COMMAND c4h_add_plugin OR NOT TARGET IOUtilities)
    message(FATAL_ERROR
      "DELPHI_BUILD_CODE4HEP_SOURCE requires an in-tree Code4hep build with IOUtilities")
  endif()
  find_package(Threads REQUIRED)
  c4h_add_plugin(
    NAME DelphiInputPlugins
    SOURCES
      code4hep/DelphiEventSummaryProducer.cc
      code4hep/DelphiCentralTrackFitProducer.cc
      code4hep/DelphiCentralTrackExtensionProducer.cc
      code4hep/DelphiCentralTrackRefitProducer.cc
      code4hep/DelphiInnerDetectorDigitizerProducer.cc
      code4hep/DelphiInnerDetectorHitReconstructionProducer.cc
      code4hep/DelphiOuterDetectorDigitizerProducer.cc
      code4hep/DelphiOuterDetectorHitReconstructionProducer.cc
      code4hep/DelphiSource.cc
      code4hep/DelphiTrackTruthProducer.cc
      code4hep/DelphiTrackerHitPartitionProducer.cc
      code4hep/DelphiVertexDigitizerProducer.cc
      code4hep/DelphiVertexHitReconstructionProducer.cc
      code4hep/DelphiTpcDigitizerProducer.cc
      code4hep/DelphiTpcHitReconstructionProducer.cc
      code4hep/DelphiTpcPadMapperProducer.cc
    DEPS
      FWCore/Framework
      FWCore/ParameterSet
      Code4hep/IOUtilities
    EXT_DEPS
      podio::podio
      EDM4HEP::edm4hep
  )
  target_include_directories(plugin_DelphiInputPlugins PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  # Do not make the plugin DT_NEEDED on libdelphi_edm4hep: that library has
  # legacy COMMON-block data references which can only be resolved by the
  # non-PIE launcher.  Function references stay lazy while edmWriteConfigs
  # and edmPluginRefresh inspect the plugin, and resolve from delphiRun
  # when the source is actually constructed.
  target_link_libraries(plugin_DelphiInputPlugins
    PUBLIC Threads::Threads
    PRIVATE delphi_geometry)

  # The DELPHI/CERNLIB archives are non-PIC and therefore belong in an
  # executable, not in the DelphiSource shared plugin.  This compact
  # Code4hep runner exports their symbols to dynamically loaded plugins.
  add_executable(delphiRun code4hep/DelphiCmsRun.cpp)
  target_link_options(delphiRun PRIVATE
    -no-pie
    -Wl,--export-dynamic
    -Wl,--undefined=phdst_)
  target_link_libraries(delphiRun PRIVATE
    delphi_edm4hep
    delphi_fortran_no_skelana_link
    Stitched::FWCore_Framework
    Stitched::FWCore_ParameterSetReader
    Stitched::FWCore_ParameterSet
    Stitched::FWCore_MessageLogger
    Stitched::FWCore_MessageService
    Stitched::FWCore_PluginManager
    Stitched::FWCore_ServiceRegistry
    Stitched::FWCore_Utilities
    Stitched::FWCore_Concurrency
    TBB::tbb
    ROOT::Core
    gfortran dl m z crypt)
endif()

# Migration-only A/B oracle. It compiles the identical fDST writer graph but
# prepares each event through PSINI/PSBEG. Deliberately OFF by default and not
# installed, so production remains provably free of libskelanaxx.
option(DELPHI_BUILD_SKELANA_REFERENCE
  "Build non-installed sDST/fDST PSBEG reference executables" OFF)
if(DELPHI_BUILD_SKELANA_REFERENCE)
  add_library(delphi_skelana_reference_adapter STATIC src/LegacySkelana.cpp)
  target_include_directories(delphi_skelana_reference_adapter
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(delphi_skelana_reference_adapter
    PRIVATE delphi_edm4hep DelphiAL9::headers)

  add_executable(delphi_sdst_skelana_reference bin/delphi_sdst_pass.cpp)
  target_compile_definitions(delphi_sdst_skelana_reference
    PRIVATE DELPHI_SDST_SKELANA_REFERENCE=1)
  target_link_libraries(delphi_sdst_skelana_reference
    PRIVATE delphi_edm4hep delphi_skelana_reference_adapter
            delphi_fortran_link)

  add_executable(delphi_fdst_skelana_reference bin/delphi_fdst_pass.cpp)
  target_compile_definitions(delphi_fdst_skelana_reference
    PRIVATE DELPHI_FDST_SKELANA_REFERENCE=1)
  target_link_libraries(delphi_fdst_skelana_reference
    PRIVATE delphi_edm4hep delphi_skelana_reference_adapter
            delphi_fortran_link)

  add_executable(delphi_fdst_skelana_init_reference bin/delphi_fdst_pass.cpp)
  target_compile_definitions(delphi_fdst_skelana_init_reference
    PRIVATE DELPHI_FDST_SKELANA_INIT_REFERENCE=1)
  target_link_libraries(delphi_fdst_skelana_init_reference
    PRIVATE delphi_edm4hep delphi_skelana_reference_adapter
            delphi_fortran_link)
endif()

# Standalone post-processing tool: per-run beamspot from aggregated PVs.
# Pure podio/ROOT — no PHDST, no DELPHI Fortran link group.
add_executable(delphi_bs_fit bin/delphi_bs_fit.cpp)
target_link_libraries(delphi_bs_fit
  PRIVATE EDM4HEP::edm4hep podio::podio podio::podioRootIO ROOT::Core ROOT::RIO
)

# Integrity/provenance check used by resumable b-tag conversion campaigns.
# Standalone EDM4hep/podio/ROOT tool: it does not load the DELPHI runtime.
add_executable(delphi_btag_check bin/delphi_btag_check.cpp)
target_include_directories(delphi_btag_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(delphi_btag_check
  PRIVATE EDM4HEP::edm4hep podio::podio podio::podioRootIO ROOT::Core ROOT::RIO
)

add_executable(delphi_geometry_audit bin/delphi_geometry_audit.cpp)
target_link_libraries(delphi_geometry_audit PRIVATE delphi_geometry)

# ---------------------------------------------------------------------------
# Tests (subtask 5+; for now just smoke).
enable_testing()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install rules.
include(GNUInstallDirs)
set(_delphi_install_targets
  delphi_edm4hep delphi_geometry delphi_sdst_pass delphi_fdst_pass
  delphi_bs_fit delphi_btag_check delphi_geometry_audit
  delphi_geometry_export delphi_tpc_readout_audit delphi_vertex_readout_audit)
list(APPEND _delphi_install_targets delphi_id_readout_audit)
if(TARGET delphiRun)
  list(APPEND _delphi_install_targets delphiRun)
endif()
install(TARGETS ${_delphi_install_targets}
  EXPORT  delphi_edm4hepTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/delphi_edm4hep
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
