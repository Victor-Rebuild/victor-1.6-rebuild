#!/bin/bash

set -e

if [[ ! -f ./CPPLINT.cfg ]]; then
    if [[ -f ../CPPLINT.cfg ]]; then
        cd ..
    else
        echo "This script must be run in the victor repo. ./wire/build.sh"
        exit 1
    fi
fi

VICDIR="$(pwd)"

cd ~
if [[ ! -d .anki ]]; then
    echo "Downloading ~/.anki folder contents..."
    git clone https://github.com/kercre123/anki-deps
    mv anki-deps .anki
fi

if [[ ${UNAME} == "Darwin" ]]; then
    echo "Checking out macOS branch..."
    cd ~/.anki
    if [[ $(uname -a) == *"arm64"* ]]; then
        git checkout macos-arm
    else
        git checkout macos
    fi
    git lfs install
    git lfs pull
else
    if [[ $(uname -a) == *"aarch64"* ]]; then
       cd ~/.anki
       git checkout arm64-linux
    fi
fi

cd "${VICDIR}"

git lfs update --force

if [[ ! -d EXTERNALS/ ]]; then
    echo "Downloading EXTERNALS folder contents..."
    wget https://modder.my.to/1.6-externals.tar.gz
    tar xzf 1.6-externals.tar.gz
    rm 1.6-externals.tar.gz
fi

echo "Building victor..."

./project/victor/scripts/victor_build_release.sh

echo "Copying vic-cloud and vic-gateway..."
cp bin/* _build/vicos/Release/bin/

echo "Copying libopus..."
cp -a EXTERNALS/deps/opus/libopus.so.0.7.0 _build/vicos/Release/lib/libopus.so.0

echo

echo "Build was successful!"
