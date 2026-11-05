#!/bin/bash
VERSION_FILE="VERSION"
README_FILE="README.md"

if [ ! -f "$VERSION_FILE" ]; then
    echo "Error: Version file '$VERSION_FILE' not found. Cannot update README."
    exit 1
fi

VERSION=$(cat "$VERSION_FILE")

sed -i.bak "s!https://img.shields.io/badge/Version-PLACEHOLDER-blue!https://img.shields.io/badge/Version-${VERSION}-blue!g" "$README_FILE"

rm "${README_FILE}.bak"

echo "Successfully updated README.md with version $VERSION."