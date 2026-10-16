#!/usr/bin/env bash
# Linux build, verification and packaging. Never installs into a live plugin directory.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$root/build-linux"
juce_dir="${ANNEAL_JUCE_DIR:-}"
jobs="${ANNEAL_JOBS:-2}"
clap=ON
test=0 snapshot=0 package=0

usage() {
    cat <<'HELP'
Usage: bash build.sh [--test] [--snapshot] [--package] [--no-clap]
                     [--build-dir DIR] [--juce-dir DIR] [--jobs N]
Builds Linux VST3, LV2, CLAP and Standalone in Release mode.
--test       Run acceptance, VST3 host and CLAP checks (Xvfb on headless machines).
--snapshot   Render the production editor into the build directory.
--package    Create the versioned Linux tar.gz in dist/ (requires CLAP).
--no-clap    Skip CLAP and its additional dependency download.
JUCE is fetched at 8.0.4 unless --juce-dir / ANNEAL_JUCE_DIR points to a local copy.
Nothing is installed. Install a packaged build with its separate install.sh.
HELP
}
while (($#)); do
    case "$1" in
        --test) test=1; shift ;;
        --snapshot) snapshot=1; shift ;;
        --package) package=1; shift ;;
        --no-clap) clap=OFF; shift ;;
        --build-dir|--juce-dir|--jobs)
            (($# >= 2)) && [[ -n "$2" && "$2" != --* ]] || { echo "Missing value for $1" >&2; exit 2; }
            case "$1" in --build-dir) build_dir="$2";; --juce-dir) juce_dir="$2";; --jobs) jobs="$2";; esac
            shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done
[[ "$(uname -s)" == Linux ]] || { echo "build.sh requires Linux; use build.ps1 on Windows." >&2; exit 2; }
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo "--jobs must be a positive integer" >&2; exit 2; }
((!package)) || [[ "$clap" == ON ]] || { echo "--package requires CLAP; omit --no-clap." >&2; exit 2; }
if [[ -n "$juce_dir" && ! -f "$juce_dir/CMakeLists.txt" ]]; then
    echo "JUCE source not found: $juce_dir" >&2; exit 2
fi
command -v cmake >/dev/null || { echo "CMake is required (3.28+ with CLAP)." >&2; exit 2; }
if ((test)) && [[ "$clap" == ON ]]; then command -v python3 >/dev/null; fi

with_display() {
    bash "$root/tools/WithDisplay.sh" "$@"
}
configure=(-S "$root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DANNEAL_COPY_AFTER_BUILD=OFF "-DANNEAL_CLAP=$clap")
[[ -z "$juce_dir" ]] || configure+=("-DANNEAL_JUCE_DIR=$juce_dir")
cmake "${configure[@]}"
targets=(Anneal_VST3 Anneal_LV2 Anneal_Standalone AnnealTests AnnealHostCheck)
[[ "$clap" == OFF ]] || targets+=(Anneal_CLAP)
((!snapshot)) || targets+=(AnnealSnapshot)
# JUCE's bundle metadata helpers load the plugin while building, so they also need a display.
with_display cmake --build "$build_dir" --config Release --parallel "$jobs" --target "${targets[@]}"
if ((test)); then
    with_display "$build_dir/AnnealTests_artefacts/Release/AnnealTests"
    with_display "$build_dir/AnnealHostCheck_artefacts/Release/AnnealHostCheck" "$build_dir/Anneal_artefacts/Release/VST3/Anneal.vst3"
    if [[ "$clap" == ON ]]; then
        with_display python3 "$root/tools/ClapCheck.py" "$build_dir/Anneal_artefacts/Release/CLAP/Anneal.clap"
    fi
fi
if ((snapshot)); then with_display "$build_dir/AnnealSnapshot_artefacts/Release/AnnealSnapshot" "$build_dir/snapshots"; fi
if ((package)); then bash "$root/packaging/linux/package.sh" "$build_dir" "$root/dist"; fi
