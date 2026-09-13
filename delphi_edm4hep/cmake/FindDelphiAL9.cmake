# FindDelphiAL9.cmake
#
# Locate — and pin — the DELPHI almalinux-9 Fortran libraries (dstana) and
# CERNLIB, plus the vendored header-only C++ wrappers around the SKELANA /
# PHDST common blocks (extern/delphi-analysis).
#
# The cvmfs release tree is full of moving symlinks (`latest` -> vNN...,
# `cern/pro` -> a dated build) and dated directories that change from release
# to release, so hardcoded paths rot: e.g. `cern/2025.09.18.4-free-im` existed
# under the release `latest` pointed to in early 2026 but not under later ones.
# This module therefore never hardcodes a dated directory. Each lib dir is
# resolved in priority order:
#
#   1. an explicit cache override        (-DDELPHI_AL9_LIB_DIR=... / -DCERN_AL9_LIB_DIR=...)
#   2. the sourced DELPHI environment    ($DELPHI_LIB / $CERN_LIB, $CERN_ROOT —
#                                         exported by /cvmfs/delphi.cern.ch/setup.sh,
#                                         which the converter needs at runtime anyway)
#   3. discovery under DELPHI_AL9_ROOT   (glob dstana/*/lib, cern/*/lib{,64})
#
# and the winner is passed through file(REAL_PATH) so the *concrete* dated
# directory — not a symlink that can move under us — is what lands in the
# cache and in the link line. Reconfigures reuse the cached pin; wipe the
# build dir (or set the -D overrides) to re-resolve.
#
# Variables:
#   DELPHI_AL9_ROOT       (cache) release tree used for step-3 discovery only
#   DELPHI_AL9_LIB_DIR    (cache) pinned dstana lib dir (libphdstxx.a et al.)
#   CERN_AL9_LIB_DIR      (cache) pinned CERNLIB lib dir (libpacklib.a et al.)
#   DELPHI_ANALYSIS_INC   (cache) phdst/skelana wrapper headers; defaults to
#                         the vendored copy in extern/delphi-analysis/include
#
# Imported targets:
#   DelphiAL9::headers         INTERFACE; include dir for phdst/*.hpp, skelana/*.hpp
#   DelphiAL9::fortran_group   INTERFACE; the full --start-group/--end-group linker
#                              list, with -L paths set. Bring in gfortran/dl/m/z/crypt
#                              yourself.
#   DelphiAL9::fortran_group_no_skelana
#                              same DELPHI group without libskelanaxx, for
#                              direct-reader executables.
#
# Sets DelphiAL9_FOUND if the headers + one canonical archive per lib dir
# (libphdstxx.a, libpacklib.a) are present.

set(DELPHI_AL9_ROOT
  "/cvmfs/delphi.cern.ch/releases/almalinux-9-x86_64/latest"
  CACHE PATH "Root of the DELPHI AL9 release tree (used only to discover lib dirs when the DELPHI env is not sourced)"
)

# Pick the first candidate directory that contains ${archive}, resolve it to
# its concrete (symlink-free) path, and store it in the ${var} cache entry.
# A pre-existing non-empty cache value (user override or previous pin) is
# kept as-is so the pin is stable across reconfigures.
function(_delphi_al9_pin_libdir var archive doc)
  if(${var})
    return()
  endif()
  foreach(_cand IN LISTS ARGN)
    if(_cand AND EXISTS "${_cand}/${archive}")
      file(REAL_PATH "${_cand}" _real)
      set(${var} "${_real}" CACHE PATH "${doc}" FORCE)
      return()
    endif()
  endforeach()
endfunction()

# --- dstana lib dir (libphdstxx.a, libskelanaxx.a, ...) ---------------------
file(GLOB _delphi_dstana_globbed "${DELPHI_AL9_ROOT}/dstana/*/lib")
list(SORT _delphi_dstana_globbed ORDER DESCENDING)  # newest dated dir first; 'prerelease' sorts after digits
_delphi_al9_pin_libdir(DELPHI_AL9_LIB_DIR libphdstxx.a
  "Pinned DELPHI AL9 dstana lib dir (contains libphdstxx.a et al.)"
  "$ENV{DELPHI_LIB}"
  ${_delphi_dstana_globbed}
)

# --- CERNLIB lib dir (libpacklib.a, libkernlib.a, ...) -----------------------
# $CERN_LIB can dangle (cern/pro may point at a cmake-layout build that ships
# lib64/ instead of lib/), hence the extra $CERN_ROOT/lib{,64} candidates.
file(GLOB _cern_globbed
  "${DELPHI_AL9_ROOT}/cern/*/lib"
  "${DELPHI_AL9_ROOT}/cern/*/lib64"
)
list(SORT _cern_globbed ORDER DESCENDING)
_delphi_al9_pin_libdir(CERN_AL9_LIB_DIR libpacklib.a
  "Pinned CERNLIB AL9 lib dir (contains libpacklib.a et al.)"
  "$ENV{CERN_LIB}"
  "$ENV{CERN_ROOT}/lib"
  "$ENV{CERN_ROOT}/lib64"
  ${_cern_globbed}
)

# --- phdst/skelana wrapper headers -------------------------------------------
# Vendored in-tree (extern/delphi-analysis, copied from delphi-nanoaod — see
# the README there), so a fresh clone configures with no flags. Override with
# -DDELPHI_ANALYSIS_INC=/path/to/include to build against an external checkout.
get_filename_component(_delphi_vendored_inc
  "${CMAKE_CURRENT_LIST_DIR}/../extern/delphi-analysis/include" ABSOLUTE
)
set(DELPHI_ANALYSIS_INC
  "${_delphi_vendored_inc}"
  CACHE PATH "Include dir for the phdst/skelana wrapper headers"
)
if(NOT EXISTS "${DELPHI_ANALYSIS_INC}/phdst/functions.hpp")
  set(DELPHI_ANALYSIS_INC "DELPHI_ANALYSIS_INC-NOTFOUND")
endif()

# Sanity check: archive presence in the pinned dirs.
find_file(_delphi_phdstxx_archive
  NAMES libphdstxx.a
  PATHS ${DELPHI_AL9_LIB_DIR}
  NO_DEFAULT_PATH
)
find_file(_delphi_packlib_archive
  NAMES libpacklib.a
  PATHS ${CERN_AL9_LIB_DIR}
  NO_DEFAULT_PATH
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DelphiAL9
  REQUIRED_VARS
    DELPHI_AL9_LIB_DIR
    CERN_AL9_LIB_DIR
    DELPHI_ANALYSIS_INC
    _delphi_phdstxx_archive
    _delphi_packlib_archive
  REASON_FAILURE_MESSAGE
    "Could not pin the DELPHI AL9 libraries. Source the DELPHI environment \
(source /cvmfs/delphi.cern.ch/setup.sh) before configuring, or point \
-DDELPHI_AL9_LIB_DIR / -DCERN_AL9_LIB_DIR at directories containing \
libphdstxx.a / libpacklib.a."
)

if(DelphiAL9_FOUND)
  message(STATUS "DelphiAL9: dstana libs pinned to  ${DELPHI_AL9_LIB_DIR}")
  message(STATUS "DelphiAL9: CERNLIB libs pinned to ${CERN_AL9_LIB_DIR}")
  message(STATUS "DelphiAL9: phdst/skelana headers  ${DELPHI_ANALYSIS_INC}")

  # Header-only target.
  if(NOT TARGET DelphiAL9::headers)
    add_library(DelphiAL9::headers INTERFACE IMPORTED)
    target_include_directories(DelphiAL9::headers
      INTERFACE "${DELPHI_ANALYSIS_INC}"
    )
  endif()

  # Fortran linker group. DELPHI archives have circular dependencies so
  # they must be wrapped in --start-group/--end-group, AND must come
  # AFTER the binary's object file on the link line (otherwise the
  # archive's default main / user00_..user99_ stubs are pulled in before
  # the user binary's overrides are seen, producing multiple-definition
  # errors).
  #
  # CMake's LINK_GROUP generator expression (3.24+) handles this: the
  # group ends up as -Wl,--start-group <-l...> -Wl,--end-group with
  # correct placement relative to object files when used through
  # target_link_libraries(... PRIVATE ...).
  if(NOT TARGET DelphiAL9::fortran_group)
    add_library(DelphiAL9::fortran_group INTERFACE IMPORTED)
    target_link_directories(DelphiAL9::fortran_group INTERFACE
      "${DELPHI_AL9_LIB_DIR}"
      "${CERN_AL9_LIB_DIR}"
    )
    target_link_libraries(DelphiAL9::fortran_group INTERFACE
      "$<LINK_GROUP:RESCAN,phdstxx,skelanaxx,dstanaxx,pxdstxx,vfclapxx,vdclapxx,ufieldxx,bsaurusxx,herlibxx,triggerxx,uhlibxx,mathlib,packlib,kernlib,ariadne,herwig59,jetset74>"
    )
  endif()

  if(NOT TARGET DelphiAL9::fortran_group_no_skelana)
    add_library(DelphiAL9::fortran_group_no_skelana INTERFACE IMPORTED)
    target_link_directories(DelphiAL9::fortran_group_no_skelana INTERFACE
      "${DELPHI_AL9_LIB_DIR}"
      "${CERN_AL9_LIB_DIR}"
    )
    target_link_libraries(DelphiAL9::fortran_group_no_skelana INTERFACE
      "$<LINK_GROUP:RESCAN,phdstxx,dstanaxx,pxdstxx,vfclapxx,vdclapxx,ufieldxx,bsaurusxx,herlibxx,triggerxx,uhlibxx,mathlib,packlib,kernlib,ariadne,herwig59,jetset74>"
    )
  endif()
endif()

mark_as_advanced(
  _delphi_phdstxx_archive
  _delphi_packlib_archive
)
