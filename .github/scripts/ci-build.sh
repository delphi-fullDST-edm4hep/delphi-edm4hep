#!/bin/bash
# CI build + test recipe. Runs inside a bare AlmaLinux 9 container with
# /cvmfs bind-mounted and the repo at /repo (see .github/workflows/).
#
#   ci-build.sh [channel]
#
#     production  the release recorded in .github/key4hep-production-release
#                 (default)
#     stable      the stable release currently resolves to
#     nightly     the nightly release currently resolves to
#
# The build system never names a stack release; the channel is chosen here,
# by CI, and the production release lives in its own one-line file.
#
# Reproduce locally on any host with apptainer and /cvmfs:
#   apptainer exec --containall --fakeroot --writable-tmpfs \
#     -B /cvmfs:/cvmfs -B "$PWD":/repo docker://almalinux:9 \
#     bash /repo/.github/scripts/ci-build.sh stable
set -e

CHANNEL=${1:-production}
# Clear our positional parameters before sourcing anything: `source setup.sh`
# with no arguments passes OURS through, and key4hep's setup.sh then warns
# "Unknown argument <channel>, it will be ignored" (and could one day treat
# it as a release name).
set --

# OS bits the toolchains expect from the base system:
#   glibc-devel                 dev files for the compiler
#   zlib-devel libxcrypt-devel  -lz / -lcrypt for the DELPHI Fortran link line
#   lz4-devel                   Arrow, pulled in by podio, resolves lz4 from
#                               the system; the LCG view does not carry it
#   which make                  the LCG view resolves CC/CXX/FC through `which`
#                               and ships no make
# Everything else (gcc, cmake, ROOT, ...) comes from cvmfs.
dnf install -y -q --setopt=install_weak_deps=False \
  glibc-devel zlib-devel libxcrypt-devel lz4-devel which make

# DELPHI first, then key4hep (same order as the README). The DELPHI profile
# prints harmless 'gcc: command not found' probe warnings in a bare container.
source /cvmfs/delphi.cern.ch/setup.sh

case "${CHANNEL}" in
  production)
    release=$(tr -d '[:space:]' < /repo/.github/key4hep-production-release)
    if [ -z "${release}" ]; then
      echo "ERROR: .github/key4hep-production-release is empty" >&2
      exit 1
    fi
    source /cvmfs/sw.hsf.org/key4hep/setup.sh -r "${release}"
    ;;
  stable)
    # No -r: the stable channel's own resolution. Never `-r latest` — that
    # directory is a stale remnant frozen at 2024-04-12 (EDM4hep 0.10.5).
    source /cvmfs/sw.hsf.org/key4hep/setup.sh
    ;;
  nightly)
    source /cvmfs/sw-nightlies.hsf.org/key4hep/setup.sh
    ;;
  *)
    echo "ERROR: unknown channel '${CHANNEL}' (production|stable|nightly)" >&2
    exit 1
    ;;
esac

unset CXXFLAGS CFLAGS LDFLAGS  # DELPHI env injects CERNLIB-era flags; see README

if [ -z "${KEY4HEP_STACK:-}" ]; then
  echo "ERROR: key4hep setup did not export \$KEY4HEP_STACK — stack not set up" >&2
  exit 1
fi
# $KEY4HEP_STACK names the concrete stack, but its shape depends on the
# channel: spack releases live under .../releases/<date>/..., while the
# nightly LCG view is .../views/<view>/<day>/<platform>/setup.sh.
case "${KEY4HEP_STACK}" in
  */releases/*)
    KEY4HEP_RELEASE=$(echo "${KEY4HEP_STACK}" | sed -n 's|.*/releases/\([^/]*\)/.*|\1|p') ;;
  */views/*)
    KEY4HEP_RELEASE=$(echo "${KEY4HEP_STACK}" | sed -n 's|.*/views/\(.*\)/setup\.sh$|\1|p') ;;
esac
if [ -z "${KEY4HEP_RELEASE:-}" ]; then
  echo "ERROR: cannot identify the release from '${KEY4HEP_STACK}'" >&2
  exit 1
fi
echo "channel ${CHANNEL} -> key4hep release ${KEY4HEP_RELEASE}"
# Marker for the workflows: written before the build so it survives a build
# failure, letting a red run still name the release it was testing.
echo "${KEY4HEP_RELEASE}" > /repo/.key4hep-resolved

cmake -S /repo/delphi_edm4hep -B /repo/build
cmake --build /repo/build -j"$(nproc)"

# Production is required to stay SKELANA-free. Check the resolved link command
# as well as the resulting symbol tables so a future source change cannot
# silently reintroduce either the archive or its event steering entry points.
for target in delphi_sdst_pass delphi_fdst_pass; do
  link=/repo/build/CMakeFiles/${target}.dir/link.txt
  if grep -q -- 'skelanaxx' "${link}"; then
    echo "ERROR: ${target} links libskelanaxx" >&2
    exit 1
  fi
  if nm "/repo/build/${target}" | grep -Eq ' (psini_|psbeg_|pshort_)$'; then
    echo "ERROR: ${target} contains a SKELANA steering entry point" >&2
    exit 1
  fi
done

ctest --test-dir /repo/build --output-on-failure

# This script runs as root inside the container with the workspace bind-mounted,
# so the build output would be root-owned on the host. Hand it back to the owner
# of the checkout.
chown -R "$(stat -c '%u:%g' /repo)" /repo/build /repo/.key4hep-resolved
