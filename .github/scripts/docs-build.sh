#!/bin/bash
# Documentation build, run on the self-hosted runner.
#
# The collection map is generated from the blessed conversion-identity
# references, so the published page describes the output the identity test
# accepted. Doxygen then builds the site around it. The key4hep release is read
# from .github/key4hep-production-release, and carries doxygen and graphviz.
#
# Overrides: DELPHI_CI_REFS (reference store).
set -e

REPO="${GITHUB_WORKSPACE:-$PWD}"
REFS="${DELPHI_CI_REFS:-/home/delphi-ci/refs}"
MAP="${REPO}/docs/collection-map"

PIN="${REPO}/.github/key4hep-production-release"
K4="$(tr -d '[:space:]' < "${PIN}" 2>/dev/null || true)"
[ -n "${K4}" ] || { echo "no key4hep release in ${PIN}" >&2; exit 1; }

# `source setup.sh` with no arguments passes ours through, so clear them first.
set --
source /cvmfs/sw.hsf.org/key4hep/setup.sh -r "${K4}" >/dev/null 2>&1 || true
[ -n "${KEY4HEP_STACK:-}" ] || { echo "key4hep ${K4} did not set up" >&2; exit 1; }

cd "${REPO}"
# The collection map reads its per-collection documentation tables out of
# doxygen's XML, and doxygen's HTML in turn embeds the rendered map, so the XML
# is generated first in a pass of its own.
(cat docs/Doxyfile; echo 'GENERATE_HTML=NO'; echo 'HTML_EXTRA_FILES=') | doxygen -

python3 "${MAP}/extract.py" --refs "${REFS}" --doxygen-xml docs/xml \
                            --out "${MAP}/collection_map.json"
python3 "${MAP}/render.py"  --map "${MAP}/collection_map.json" \
                            --out "${MAP}/collection_map.html"
doxygen docs/Doxyfile
