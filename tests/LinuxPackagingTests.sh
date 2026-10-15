#!/usr/bin/env bash
# Filesystem regressions using fixture payloads, never a live plugin installation.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
scratch="$(mktemp -d "${TMPDIR:-/tmp}/anneal-linux-tests.XXXXXX")"
case "$scratch" in "${TMPDIR:-/tmp}"/anneal-linux-tests.*) ;; *) echo "Unsafe test directory" >&2; exit 1;; esac
trap 'rm -rf -- "${scratch:?}"' EXIT
checks=0 failures=0
check() { checks=$((checks + 1)); if "$@"; then echo "[PASS] $*"; else echo "[FAIL] $*"; failures=$((failures + 1)); fi; }
payload="$scratch/package with spaces"
home="$scratch/home with spaces"
mkdir -p "$payload" "$home"
cp "$root/packaging/linux/install.sh" "$payload/install.sh"
if HOME="$home" bash "$payload/install.sh" > "$scratch/empty.log" 2>&1; then empty_failed=false; else empty_failed=true; fi
check "$empty_failed"

mkdir -p "$payload/Anneal.vst3/Contents/x86_64-linux" "$payload/Anneal.lv2"
printf 'vst3 fixture' > "$payload/Anneal.vst3/Contents/x86_64-linux/Anneal.so"
printf 'lv2 fixture' > "$payload/Anneal.lv2/Anneal.so"
printf 'manifest fixture' > "$payload/Anneal.lv2/manifest.ttl"
printf 'clap fixture' > "$payload/Anneal.clap"
printf '#!/bin/sh\nexit 0\n' > "$payload/Anneal"
chmod +x "$payload/Anneal"
HOME="$home" bash "$payload/install.sh" > "$scratch/install.log"
check cmp "$payload/Anneal.vst3/Contents/x86_64-linux/Anneal.so" "$home/.vst3/Anneal.vst3/Contents/x86_64-linux/Anneal.so"
check cmp "$payload/Anneal.lv2/manifest.ttl" "$home/.lv2/Anneal.lv2/manifest.ttl"
check cmp "$payload/Anneal.clap" "$home/.clap/Anneal.clap"
check test -x "$home/.local/bin/Anneal"
printf 'other plugin' > "$home/.vst3/OtherPlugin"
printf 'updated fixture' > "$payload/Anneal.vst3/Contents/x86_64-linux/Anneal.so"
HOME="$home" bash "$payload/install.sh" > "$scratch/update.log"
check cmp "$payload/Anneal.vst3/Contents/x86_64-linux/Anneal.so" "$home/.vst3/Anneal.vst3/Contents/x86_64-linux/Anneal.so"
check test -f "$home/.vst3/OtherPlugin"

# A real copy error used to delete the user's previous plugin before cp failed.
mkdir "$scratch/bin"
cat > "$scratch/bin/cp" <<'FAILCOPY'
#!/usr/bin/env bash
echo 'Injected disk/copy failure' >&2
exit 1
FAILCOPY
chmod +x "$scratch/bin/cp"
if HOME="$home" PATH="$scratch/bin:$PATH" bash "$payload/install.sh" > "$scratch/copy-fail.log" 2>&1; then copy_failed=false; else copy_failed=true; fi
check "$copy_failed"
check test -f "$home/.vst3/Anneal.vst3/Contents/x86_64-linux/Anneal.so"
if [[ -f "$home/.vst3/Anneal.vst3/Contents/x86_64-linux/Anneal.so" ]]; then
    check cmp "$payload/Anneal.vst3/Contents/x86_64-linux/Anneal.so" "$home/.vst3/Anneal.vst3/Contents/x86_64-linux/Anneal.so"
fi
check test -f "$home/.vst3/OtherPlugin"

build="$scratch/build with spaces"
out="$scratch/releases with spaces"
a="$build/Anneal_artefacts/Release"
mkdir -p "$a/VST3" "$a/LV2" "$a/CLAP" "$a/Standalone"
cp -R "$payload/Anneal.vst3" "$a/VST3/"
cp -R "$payload/Anneal.lv2" "$a/LV2/"
cp "$payload/Anneal.clap" "$a/CLAP/"
cp "$payload/Anneal" "$a/Standalone/"
bash "$root/packaging/linux/package.sh" "$build" "$out" > "$scratch/package.log"
archive="$(find "$out" -maxdepth 1 -name '*.tar.gz' -print)"
check test -f "$archive"
check test -f "$archive.sha256"
(cd "$out" && sha256sum -c "$(basename "$archive").sha256")
unpack="$scratch/unpacked"
mkdir "$unpack"
tar -xzf "$archive" -C "$unpack"
release="$(find "$unpack" -mindepth 1 -maxdepth 1 -type d -print)"
check test -f "$release/Anneal.vst3/Contents/x86_64-linux/Anneal.so"
check test -f "$release/Anneal.lv2/manifest.ttl"
check test -f "$release/Anneal.clap"
check test -x "$release/Anneal"
check test -x "$release/install.sh"
if grep -q '@VERSION@' "$release/INSTALL.txt"; then version_replaced=false; else version_replaced=true; fi
check "$version_replaced"
hash_before="$(sha256sum "$archive")"
rm "$a/CLAP/Anneal.clap"
if bash "$root/packaging/linux/package.sh" "$build" "$out" > "$scratch/missing.log" 2>&1; then missing_failed=false; else missing_failed=true; fi
check "$missing_failed"
check test "$(sha256sum "$archive")" = "$hash_before"
check bash "$root/build.sh" --help
if bash "$root/build.sh" --build-dir > "$scratch/args.log" 2>&1; then args_failed=false; else args_failed=true; fi
check "$args_failed"
echo "$checks Linux packaging checks, $failures failed (fixture payloads; no DSP claim)"
exit "$failures"
