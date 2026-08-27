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

# DELPHI almalinux-9 Fortran libraries (custom find module).
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
find_package(DelphiAL9 REQUIRED)

# ---------------------------------------------------------------------------
# delphi_edm4hep library: pure C++, depends on edm4hep + podio + ROOT +
# DelphiAL9. No Fortran symbols are pulled in here directly; the Fortran
# linkage happens via the binaries (which need the full --start-group
# linker dance because the DELPHI archives have circular symbol deps).
add_library(delphi_edm4hep
  src/BankPrefix.cpp
  src/PerigeeMatch.cpp
  src/Helix.cpp
  src/TeBank.cpp
  src/PhdstHarness.cpp
  src/Event/Event.cpp
  src/Vertex/Vertex.cpp
  src/Btag/Btag.cpp
  src/Tracking/Tracking.cpp
  src/Calorimeter/Calorimeter.cpp
  src/Pid/ParticleId.cpp
  src/Truth/Truth.cpp
  src/Tracking/MatchProvenance.cpp
  src/Pid/TofFdst.cpp
  src/Pid/MtpcFdst.cpp
  src/Tracking/TeStateMerge.cpp
  src/Calorimeter/EmcaFdst.cpp
  src/Calorimeter/HcalFdst.cpp
  src/Calorimeter/TeadFdst.cpp
  src/Tracking/MainHybrid.cpp
  src/Pid/PidHybrid.cpp
  src/Calorimeter/ShowerHybrid.cpp
  src/Truth/TblHybrid.cpp
  src/Pid/FdstPidExtras.cpp
  src/Calorimeter/TdhaFdst.cpp
  src/Pid/SdstPaExtras.cpp
  src/Calorimeter/SticShower.cpp
  src/Tracking/EltrSdst.cpp
  src/Tracking/TraxFdst.cpp
  src/Tracking/VdHits.cpp
  src/Pid/PidExtrasSdst.cpp
  src/Calorimeter/CcalFdst.cpp
)
target_include_directories(delphi_edm4hep
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
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

# ---------------------------------------------------------------------------
# Pass-1 binary (sDST -> intermediate edm4hep).
add_executable(delphi_sdst_pass bin/delphi_sdst_pass.cpp)
target_link_libraries(delphi_sdst_pass
  PRIVATE delphi_edm4hep delphi_fortran_link
)

# Pass-2 binary (intermediate edm4hep + fDST -> final edm4hep).
add_executable(delphi_fdst_pass bin/delphi_fdst_pass.cpp)
target_link_libraries(delphi_fdst_pass
  PRIVATE delphi_edm4hep delphi_fortran_link
)

# Standalone post-processing tool: per-run beamspot from aggregated PVs.
# Pure podio/ROOT — no PHDST, no DELPHI Fortran link group.
add_executable(delphi_bs_fit bin/delphi_bs_fit.cpp)
target_link_libraries(delphi_bs_fit
  PRIVATE EDM4HEP::edm4hep podio::podio podio::podioRootIO ROOT::Core ROOT::RIO
)

# Integrity/provenance check used by resumable b-tag conversion campaigns.
# Standalone EDM4hep/podio/ROOT tool: it does not load the DELPHI runtime.
add_executable(delphi_btag_check bin/delphi_btag_check.cpp)
target_include_directories(delphi_btag_check PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(delphi_btag_check
  PRIVATE EDM4HEP::edm4hep podio::podio podio::podioRootIO ROOT::Core ROOT::RIO
)

# ---------------------------------------------------------------------------
# Tests (subtask 5+; for now just smoke).
enable_testing()
if(EXISTS ${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt)
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install rules.
include(GNUInstallDirs)
install(TARGETS delphi_edm4hep delphi_sdst_pass delphi_fdst_pass delphi_bs_fit
                delphi_btag_check
  EXPORT  delphi_edm4hepTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/delphi_edm4hep
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
