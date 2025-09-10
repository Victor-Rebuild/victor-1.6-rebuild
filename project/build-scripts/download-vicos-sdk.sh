#!/bin/bash

TOOLCHAIN_VERSION_DEFAULT="5.2.1-r06"

if [[ $1 != "" ]]; then
    TOOLCHAIN_VERSION="$1"
else
    TOOLCHAIN_VERSION=$TOOLCHAIN_VERSION_DEFAULT
fi

ADEPS="$HOME/.anki"

if [[ "$(uname -a)" == *"x86_64"* && "$(uname -a)" == *"Linux"* ]]; then
    HOST="amd64-linux"
    elif [[ "$(uname -a)" == *"arm64"* && "$(uname -a)" == *"Darwin"* ]]; then
    HOST="arm64-macos"
    elif [[ "$(uname -a)" == *"aarch64"* && "$(uname -a)" == *"Linux"* ]]; then
    HOST="arm64-linux"
else
    echo "This can only be run on x86_64 Linux, arm64 Linux, or arm64 macOS systems at the moment."
    echo "This will be fixed once I compile the new toolchain for more platforms."
    exit 1
fi

echo "Platform: $HOST"

if [[ ! -d "$ADEPS/vicos-sdk/dist/${TOOLCHAIN_VERSION}/prebuilt" ]]; then
    mkdir -p "$ADEPS/vicos-sdk/dist/${TOOLCHAIN_VERSION}"
    cd "$ADEPS/vicos-sdk/dist/${TOOLCHAIN_VERSION}"
    wget -q --show-progress https://github.com/os-vector/wire-os-externals/releases/download/${TOOLCHAIN_VERSION}/vicos-sdk_${TOOLCHAIN_VERSION}_$HOST.tar.gz
    tar -zxf vicos-sdk_${TOOLCHAIN_VERSION}_$HOST.tar.gz
    rm -f vicos-sdk_${TOOLCHAIN_VERSION}_$HOST.tar.gz
fi

if [[ ! -d "$ADEPS/wwise/versions/2017.2.7_a" ]]; then
    mkdir -p "$ADEPS/wwise/versions/2017.2.7_a"
    cd "$ADEPS/wwise/versions/2017.2.7_a"
    wget -q --show-progress https://github.com/os-vector/wire-os-externals/releases/download/4.0.0-r05/wwise-2017.2.7_a.tar.gz
    tar -zxf wwise-2017.2.7_a.tar.gz
    rm -f wwise-2017.2.7_a.tar.gz
fi
