#!/bin/bash
# Installs Anneal for the current user into the folders Linux hosts scan by default.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"

install_item() (   # install_item <item> <destination folder>; trap is local to this subshell
    local item="$1" dest="$2"
    if [ ! -e "$here/$item" ]; then
        echo "Skipped $item (not in this folder)"
        return
    fi
    mkdir -p "$dest"
    local stage target
    target="$dest/$item"
    stage="$(mktemp -d "$dest/.anneal-install.XXXXXX")"
    cleanup() {
        if [[ -e "$stage/previous" || -L "$stage/previous" ]] && [[ ! -e "$target" && ! -L "$target" ]]; then
            if ! mv -- "$stage/previous" "$target"; then
                echo "Could not restore $target; previous installation kept at $stage/previous" >&2
                return
            fi
        fi
        rm -rf -- "${stage:?}"
    }
    trap cleanup EXIT
    # Complete the copy on the destination filesystem before replacing the working plugin.
    cp -R -- "$here/$item" "$stage/$item"
    if [[ -e "$target" || -L "$target" ]]; then mv -- "$target" "$stage/previous"; fi
    mv -- "$stage/$item" "$target"
    echo "Installed $dest/$item"
)

found=0
for item in Anneal.vst3 Anneal.lv2 Anneal.clap Anneal; do
    if [[ -e "$here/$item" ]]; then found=1; break; fi
done
((found)) || { echo "No Anneal binaries found beside install.sh. Extract the complete Linux archive first." >&2; exit 1; }

install_item Anneal.vst3 "$HOME/.vst3"
install_item Anneal.lv2  "$HOME/.lv2"
install_item Anneal.clap "$HOME/.clap"
install_item Anneal      "$HOME/.local/bin"

echo
echo "Done. Restart your DAW to see Anneal."
