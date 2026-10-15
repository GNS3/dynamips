#!/usr/bin/env bash
set -euo pipefail

# Setting up ARM emulation on Linux amd64 host:
# sudo apt-get install qemu-user-binfmt
# sudo docker run --rm --privileged multiarch/qemu-user-static --reset -p yes

usage() {
    printf 'Usage: %s IOS_DIRECTORY [COMMAND [ARG...]]\n' "$0"
    printf 'Build Dynamips in Ubuntu ARM64, mount IOS_DIRECTORY at /ios (read-only),\n'
    printf 'then run COMMAND (default: /bin/bash). Requires Docker and ARM64 support.\n'
}

if [[ ${1:-} == --help || ${1:-} == -h ]]; then
    usage
    exit 0
fi

if [[ $# -eq 0 ]]; then
    usage >&2
    exit 1
fi

if ! command -v docker >/dev/null 2>&1; then
    printf 'Error: Docker is required but was not found in PATH.\n' >&2
    exit 1
fi

if [[ ! -d $1 ]]; then
    printf 'Error: IOS directory does not exist: %s\n' "$1" >&2
    exit 1
fi

ios_directory=$(cd -- "$1" && pwd -P)
source_directory=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
shift

if [[ $# -eq 0 ]]; then
    set -- /bin/bash
fi

docker_options=(--rm --interactive --platform linux/arm64)
if [[ -t 0 && -t 1 ]]; then
    docker_options+=(--tty)
fi

exec docker run "${docker_options[@]}" \
    --mount "type=bind,source=$source_directory,target=/src,readonly" \
    --mount "type=bind,source=$ios_directory,target=/ios,readonly" \
    --workdir /work \
    ubuntu:24.04 /bin/bash -euo pipefail -c '
        if [[ $(dpkg --print-architecture) != arm64 ]]; then
            printf "Error: expected an ARM64 Ubuntu container.\n" >&2
            exit 1
        fi

        export DEBIAN_FRONTEND=noninteractive
        apt-get update
        apt-get install -y --no-install-recommends \
            build-essential cmake libelf-dev libpcap-dev
        cmake -S /src -B /build \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DDYNAMIPS_ARCH=arm64 \
            -DDYNAMIPS_CODE=stable
        cmake --build /build --parallel "$(nproc)"
        cmake --install /build

        printf "\nDynamips ARM64 is ready. IOS images: /ios; writable runtime files: /work.\n"
        exec "$@"
    ' -- "$@"
