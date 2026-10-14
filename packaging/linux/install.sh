#!/bin/bash
# Installs Anneal for the current user into the folders Linux hosts scan by default.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"

install_item() {   # install_item <item> <destination folder>
    local item="$1" dest="$2"
    if [ ! -e "$here/$item" ]; then
        echo "Skipped $item (not in this folder)"
        return
    fi
    mkdir -p "$dest"
    rm -rf "${dest:?}/$item"
    cp -R "$here/$item" "$dest/"
    echo "Installed $dest/$item"
}

install_item Anneal.vst3 "$HOME/.vst3"
install_item Anneal.lv2  "$HOME/.lv2"
install_item Anneal.clap "$HOME/.clap"
install_item Anneal      "$HOME/.local/bin"

echo
echo "Done. Restart your DAW to see Anneal."
