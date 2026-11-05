#!/bin/sh
# Build aterminal and install it, or build a .deb package.
set -eu

usage() {
    cat <<USAGE
Usage: ./install.sh [--user | --prefix DIR | --deb | --rpm] [--help]

  (no option)    Install to /usr/local, using sudo if needed
  --user         Install to \$HOME/.local without root access
  --prefix DIR   Install to DIR
  --deb          Build a .deb package in build/ instead of installing
  --rpm          Build an .rpm package in build/ instead of installing
USAGE
}

prefix=/usr/local
package=""
while [ $# -gt 0 ]; do
    case "$1" in
        --user) prefix="$HOME/.local" ;;
        --prefix)
            [ $# -ge 2 ] || { echo "install.sh: --prefix needs a directory" >&2; exit 2; }
            prefix="$2"; shift ;;
        --deb) package=DEB ;;
        --rpm) package=RPM ;;
        -h|--help) usage; exit 0 ;;
        *) echo "install.sh: unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"

if [ -n "$package" ]; then
    (cd build && cpack -G "$package")
    exit 0
fi

# Only the install step needs root, so build files stay owned by the user.
parent="$prefix"
while [ ! -e "$parent" ]; do parent="$(dirname "$parent")"; done
as_root=""
[ -w "$parent" ] || as_root=sudo

$as_root cmake --install build --prefix "$prefix"
if command -v update-desktop-database >/dev/null 2>&1; then
    $as_root update-desktop-database -q "$prefix/share/applications" || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1 && [ -f "$prefix/share/icons/hicolor/index.theme" ]; then
    $as_root gtk-update-icon-cache -q -f -t "$prefix/share/icons/hicolor" || true
fi
if [ "$prefix" = /usr/local ] && command -v mandb >/dev/null 2>&1; then
    $as_root mandb -q || true
fi
echo "Installed aterminal to $prefix"
