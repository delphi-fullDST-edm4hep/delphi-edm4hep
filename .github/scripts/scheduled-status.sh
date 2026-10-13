#!/bin/bash
# Mirror the result of a scheduled workflow into one tracking issue, so a red
# nightly or bump check is visible to everyone watching the repository. GitHub
# itself only notifies whoever last edited the workflow's cron line.
#
#   scheduled-status.sh <job status>      # success | failure | cancelled
#
# failure  opens the workflow's tracking issue, or comments on the open one
# success  closes the open tracking issue, if any
# other    nothing (a cancelled run says nothing about the stack)
#
# Runs on the GitHub-hosted runner, not in the build container. Needs GH_TOKEN
# with issues: write; the GITHUB_* variables are set by Actions.
set -euo pipefail

STATUS=${1:?usage: scheduled-status.sh <job status>}
LABEL=ci-scheduled-failure
TITLE="Scheduled CI failing: ${GITHUB_WORKFLOW}"
RUN_URL="${GITHUB_SERVER_URL}/${GITHUB_REPOSITORY}/actions/runs/${GITHUB_RUN_ID}"
# Written by ci-build.sh before it builds, so it names the stack even when the
# build failed. Absent if the run died earlier (e.g. while mounting cvmfs).
STACK=$(cat .key4hep-resolved 2>/dev/null || echo "unknown")

issue=$(gh issue list --repo "${GITHUB_REPOSITORY}" --state open \
          --label "${LABEL}" --json number,title --limit 100 |
        jq -r --arg title "${TITLE}" \
          'map(select(.title == $title)) | first | .number // empty')

case "${STATUS}" in
  failure)
    if [ -n "${issue}" ]; then
      gh issue comment "${issue}" --repo "${GITHUB_REPOSITORY}" \
        --body "Still failing: ${RUN_URL} (key4hep \`${STACK}\`)."
    else
      gh label create "${LABEL}" --repo "${GITHUB_REPOSITORY}" --force \
        --color B60205 --description "A scheduled CI workflow is failing"
      gh issue create --repo "${GITHUB_REPOSITORY}" --title "${TITLE}" \
        --label "${LABEL}" --body "\
The scheduled **${GITHUB_WORKFLOW}** workflow failed: ${RUN_URL}

key4hep stack: \`${STACK}\`

This issue is updated by every failing scheduled run and closed automatically
by the next successful one."
    fi
    ;;
  success)
    if [ -n "${issue}" ]; then
      gh issue close "${issue}" --repo "${GITHUB_REPOSITORY}" \
        --comment "Passing again: ${RUN_URL} (key4hep \`${STACK}\`)."
    fi
    ;;
  *)
    echo "job status '${STATUS}': leaving the tracking issue alone"
    ;;
esac
