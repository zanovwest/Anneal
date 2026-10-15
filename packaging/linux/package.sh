#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
[[ $# == 2 ]] || { echo "Usage: package.sh BUILD_DIR OUTPUT_DIR" >&2; exit 2; }
build="$(cd -- "$1" && pwd)"
mkdir -p -- "$2"
out="$(cd -- "$2" && pwd)"
version="$(sed -nE 's/^project\(Anneal VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$root/CMakeLists.txt")"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Cannot read Anneal version" >&2; exit 2; }
artefacts="$build/Anneal_artefacts/Release"
for item in VST3/Anneal.vst3 LV2/Anneal.lv2 CLAP/Anneal.clap Standalone/Anneal; do
    [[ -e "$artefacts/$item" ]] || { echo "Missing release artefact: $item" >&2; exit 1; }
done
staging="$(mktemp -d "$out/.anneal-package.XXXXXX")"
trap 'rm -rf -- "${staging:?}"' EXIT
name="Anneal-$version-Linux-x86_64"
mkdir "$staging/$name"
for item in VST3/Anneal.vst3 LV2/Anneal.lv2 CLAP/Anneal.clap Standalone/Anneal; do
    cp -R -- "$artefacts/$item" "$staging/$name/"
done
sed "s/@VERSION@/$version/g" "$root/packaging/linux/INSTALL.txt" > "$staging/$name/INSTALL.txt"
cp -- "$root/packaging/linux/install.sh" "$staging/$name/install.sh"
chmod +x "$staging/$name/install.sh" "$staging/$name/Anneal"
tar -czf "$staging/$name.tar.gz" -C "$staging" "$name"
mv -- "$staging/$name.tar.gz" "$out/$name.tar.gz"
(cd -- "$out" && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")
echo "$out/$name.tar.gz"
