#!/usr/bin/env bash

CURRENT_BUILDER="vic-standalone-builder-8"

set -e

if [[ $(id -u) == 0 ]]; then
	echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    echo -e "\033[1;31mDo not run this script as root.\033[0m"
    echo "If Docker is giving you a permission denied error, look over the README one more time. It includes instructions to allow Docker to be run as a normal user."
	echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    exit 1
fi

if [[ "$(uname -a)" == *"Darwin"* ]]; then
    echo "macOS building does not work right now. This will be fixed soon."
    exit 0
    ./project/victor/scripts/victor_build_release.sh "$@"
    echo
    echo -e "\033[1;32mComplete.\033[0m"
    echo
else
    if [[ -d build/cache/0 ]]; then
        echo "Rebuilding cache..."
        rm -rf build/cache
        # permissions prevent us from deleting as sudo. this
        # is fixed in build/cache/go thanks to -modcacherw
        #rm -rf build/gocache
        rm -rf build/usercache
    fi
    mkdir -p anki-deps
    mkdir -p build/cache/ccache
    mkdir -p build/cache/go
    mkdir -p build/cache/user
	if [[ ! -z $(docker images -q victor-${USER}) ]]; then
		echo "Purging old legacy victor container..."
		docker ps -a --filter "ancestor=victor-${USER}" -q | xargs -r docker rm -f
		docker ps -a --filter "ancestor=victor" -q | xargs -r docker rm -f
		echo
		echo -e "\033[5m\033[1m\033[31mOld Docker builder detected on system. If you have built victor or wire-os many times, it is recommended you run:\033[0m"
		echo
		echo -e "\033[1m\033[36mdocker system prune -a --volumes\033[0m"
		echo
		echo -e "\033[32mContinuing in 5 seconds... (you will only see this message once)\033[0m"
		sleep 5
	fi
    if [[ -z $(docker images -q $CURRENT_BUILDER) ]]; then
        docker build \
        --build-arg DIR_PATH="$(pwd)" \
        --build-arg USER_NAME=$USER \
        --build-arg UID=$(id -u $USER) \
        --build-arg GID=$(id -u $USER) \
        -t $CURRENT_BUILDER \
        build/
    else
        echo "Reusing $CURRENT_BUILDER"
    fi
    docker run --rm -it \
    -v $(pwd)/anki-deps:/home/$USER/.anki \
    -v $(pwd):$(pwd) \
    -v $(pwd)/build/cache/ccache:/home/$USER/.ccache \
    -v $(pwd)/build/cache/go:/home/$USER/go \
    -v $(pwd)/build/cache/user:/home/$USER/.cache \
    $CURRENT_BUILDER bash -c \
    "cd $(pwd) && \
		./project/victor/scripts/victor_build_release.sh $@ && \
		echo && \
		echo -e \"\e[1;32mComplete.\e[0m\" && \
    echo"
fi