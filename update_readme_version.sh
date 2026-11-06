#!/bin/sh
# Updates the README version badge from the VERSION file.
set -eu

cd "$(dirname "$0")"
version=$(cat VERSION)
sed -i -E "s|badge/Version-[^-]+-blue|badge/Version-$version-blue|" README.md
echo "README badge set to $version"
