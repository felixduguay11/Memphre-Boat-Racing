#!/usr/bin/env bash
# flash_teensy.sh — compile et televerse le firmware du Teensy depuis le Pi.
#
# Usage :   ./flash_teensy.sh            compile + televerse
#           ./flash_teensy.sh --build    compile seulement (verifier avant d'aller a l'eau)
#
# Le dossier du firmware se regle ici, ou a l'appel :
#           FW_DIR=~/autre/dossier ./flash_teensy.sh
set -euo pipefail

FW_DIR="${FW_DIR:-$HOME/Memphre-Boat-Racing/Real_V1}"
APP_PATTERN='run\.py'          # processus de l'UI qui tient /dev/ttyACM0

export PATH="$PATH:$HOME/.platformio/penv/bin"

die() { echo "ERREUR : $*" >&2; exit 1; }

command -v pio >/dev/null || die "pio introuvable. Installer PlatformIO Core (voir README)."
[ -f "$FW_DIR/platformio.ini" ] || die "pas de platformio.ini dans $FW_DIR (regler FW_DIR)."
cd "$FW_DIR"

if [ "${1:-}" = "--build" ]; then
    pio run
    echo "Compilation OK, rien n'a ete televerse."
    exit 0
fi

# 1. Compiler d'abord : si ca ne compile pas, l'app n'est pas fermee pour rien.
pio run

# 2. Liberer le port serie : l'UI le tient ouvert tant qu'elle tourne.
if pgrep -f "$APP_PATTERN" >/dev/null; then
    echo "Fermeture de l'UI (elle tient le port serie)..."
    pkill -f "$APP_PATTERN" || true
    for _ in 1 2 3 4 5; do
        pgrep -f "$APP_PATTERN" >/dev/null || break
        sleep 1
    done
    pgrep -f "$APP_PATTERN" >/dev/null && die "l'UI ne se ferme pas, la fermer a la main."
fi

# 3. Televerser.
echo "Televersement... (si rien ne se passe : appuyer sur le bouton blanc du Teensy)"
pio run -t upload

echo
echo "Teensy a jour. Relancer l'UI."
