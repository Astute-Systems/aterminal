#!/bin/bash
# Cross-compiles aterminal for arm64 and packages it as a .deb in build-arm64/.
# Run as root on an amd64 Ubuntu 22.04 or later; it changes the apt sources.
set -euo pipefail

cd "$(dirname "$0")/.."
export DEBIAN_FRONTEND=noninteractive
. /etc/os-release

dpkg --add-architecture arm64
# Ubuntu serves arm64 from ports.ubuntu.com, so the main sources must stay amd64-only.
if [ -f /etc/apt/sources.list.d/ubuntu.sources ]; then
    sed -i '/^Architectures:/d; /^Types:/a Architectures: amd64' /etc/apt/sources.list.d/ubuntu.sources
fi
if [ -f /etc/apt/sources.list ]; then
    sed -i -E '/^deb(-src)? +[^[]/ s/^(deb(-src)?) +/\1 [arch=amd64] /' /etc/apt/sources.list
fi
cat > /etc/apt/sources.list.d/ubuntu-ports-arm64.sources <<EOF
Types: deb
URIs: http://ports.ubuntu.com/ubuntu-ports
Suites: ${VERSION_CODENAME} ${VERSION_CODENAME}-updates ${VERSION_CODENAME}-security
Components: main universe
Architectures: arm64
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOF

# Ubuntu's mirrors briefly serve mismatched files while they sync, so retry.
for attempt in 1 2 3 4 5; do
    apt-get -o Acquire::Retries=3 update &&
        apt-get -o Acquire::Retries=3 install -y build-essential cmake dpkg-dev file \
            gcc-aarch64-linux-gnu g++-aarch64-linux-gnu libsdl2-dev:arm64 && break
    [ "$attempt" = 5 ] && exit 1
    sleep 60
done

cmake -S . -B build-arm64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=packaging/toolchain-arm64.cmake
cmake --build build-arm64 -j "$(nproc)"
file build-arm64/aterminal | tee /dev/stderr | grep -q 'ARM aarch64'

# The tests need arm64 hardware, so the native builds run them instead.
cd build-arm64
# Tells dpkg-shlibdeps to use the arm64 objdump and libraries.
DEB_HOST_ARCH=arm64 cpack -G DEB -D CPACK_DEBIAN_PACKAGE_ARCHITECTURE=arm64
for package in *.deb; do
    dpkg-deb --field "$package" Architecture | grep -qx arm64
    mv "$package" "${package%.deb}_ubuntu-${VERSION_ID}.deb"
done
