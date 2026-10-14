#!/bin/bash
# Installs Anneal for the current user (no administrator password needed) and removes the
# download quarantine from the installed copies, because this build isn't notarized yet.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
plugins="$HOME/Library/Audio/Plug-Ins"

install_item() {   # install_item <item> <destination folder>
    local item="$1" dest="$2"
    if [ ! -e "$here/$item" ]; then
        echo "Skipped $item (not in this folder)"
        return
    fi
    mkdir -p "$dest"
    rm -rf "$dest/$item"
    cp -R "$here/$item" "$dest/"
    xattr -dr com.apple.quarantine "$dest/$item" 2>/dev/null || true
    echo "Installed $dest/$item"
}

install_item Anneal.component "$plugins/Components"
install_item Anneal.vst3      "$plugins/VST3"
install_item Anneal.clap      "$plugins/CLAP"
install_item Anneal.app       "$HOME/Applications"

# Make macOS re-read the Audio Unit list on the next scan.
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo
echo "Done. Restart your DAW to see Anneal."
