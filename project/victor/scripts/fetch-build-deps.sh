#!/bin/bash
set -e
set -u

SCRIPT_PATH=$(dirname $([ -L $0 ] && echo "$(dirname $0)/$(readlink -n $0)" || echo $0))

set +e
CURRENT_EXTERNALS_VERSION=$(cat "EXTERNALS/VERSION") >> /dev/null
set -e
EXTERNALS_VERSION_LATEST=$(curl https://raw.githubusercontent.com/Switch-modder/victor-1.6-rebuild-externals/refs/heads/main/VERSION) >> /dev/null

GIT=`which git`
if [ -z $GIT ]; then
  echo git not found
  exit 1
fi
: ${TOPLEVEL:=`$GIT rev-parse --show-toplevel`}

function vlog()
{
    echo "[fetch-build-deps] $*"
}

pushd "${TOPLEVEL}" > /dev/null 2>&1

OS_NAME=$(uname -s)
case $OS_NAME in
    "Darwin")
        HOST="mac"
        ;;
    "Linux")
        HOST="linux"
        ;;
esac

if [[ -d EXTERNALS/ ]]; then
    if [ $EXTERNALS_UPDATE_SKIP != 1 ]; then
        if [ "$CURRENT_EXTERNALS_VERSION" != "$EXTERNALS_VERSION_LATEST" ]; then
            echo "Old EXTERNALS version found"
            echo "Updating EXTERNALS"
            cd EXTERNALS/
            git pull
        else
            echo "EXTERNALS up to date (Latest version = $EXTERNALS_VERSION_LATEST)"
        fi
    else
        echo "EXTERNALS update skip detected, not updating EXTERNALS"
    fi
else
    echo "This repo was cloned incorrectly, please reclone with the "--recurse-submodules" flag"
fi

HOST_FETCH_DEPS=${SCRIPT_PATH}/"fetch-build-deps.${HOST}.sh"

if [ -x "${HOST_FETCH_DEPS}" ]; then
    ${HOST_FETCH_DEPS}
else
    echo "ERROR: Could not determine platform for system name: $OS_NAME"
    exit 1
fi
