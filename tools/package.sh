#!/bin/sh
# The release zip, release/carma2hd.zip, built from the committed tree (HEAD),
# never from uncommitted work. In a carma2hd folder: dinput.dll,
# dgVoodoo.conf and the docs. dgVoodoo2's own DLLs are not included: the
# README tells you which ones to take from dgVoodoo2's releases.
set -e
cd "$(dirname "$0")/.."
version=$(sed -n 's/^#define C2HD_VERSION "\(.*\)"/\1/p' src/version.h)
tree=$(mktemp -d)
trap 'rm -rf "$tree"' EXIT
git archive HEAD | tar -x -C "$tree"
(cd "$tree" && ./build.sh >/dev/null)
mkdir -p "$tree/zip" release
cp "$tree/build/dinput.dll" "$tree/dist/dgVoodoo.conf" \
   "$tree/README.md" "$tree/CHANGELOG.md" "$tree/LICENSE" "$tree/zip/"
rm -f release/carma2hd.zip
python -c 'import os, sys, zipfile
z = zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED)
for name in sorted(os.listdir(sys.argv[2])):
    info = zipfile.ZipInfo("carma2hd/" + name)
    info.external_attr = (0o755 if name.endswith(".sh") else 0o644) << 16
    with open(os.path.join(sys.argv[2], name), "rb") as f:
        z.writestr(info, f.read(), zipfile.ZIP_DEFLATED)' release/carma2hd.zip "$tree/zip"
echo "release/carma2hd.zip ($version, HEAD $(git rev-parse --short HEAD))"
