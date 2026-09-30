# -*- coding: utf-8 -*-
"""
config.py — tout ce qui se regle sans toucher au code de l'UI.

Ajouter une metrique = ajouter une ligne dans TOP_METRICS.
Ajouter un ESC = ajouter son id dans ESC_IDS.
"""

from dataclasses import dataclass
from typing import Callable, List

# ------------------------------------------------------------ liaison
# MODE REEL : le Teensy est branche en USB sur le Pi -> /dev/ttyACM0.
# Le chemin stable (recommande une fois teste) s'obtient avec :
#     ls /dev/serial/by-id/
# puis remplacer par p.ex.
#     SERIAL_PORT = "/dev/serial/by-id/usb-Teensyduino_USB_Serial_XXXXXXX-if00"
SERIAL_PORT = "/dev/ttyACM0"
LOG_DIR = "~/memphre_logs"

# Mise a jour du Teensy depuis l'ecran (bouton sur l'accueil).
# FW_DIR = dossier qui contient platformio.ini.
# PIO_BIN vide = cherche pio dans le PATH puis dans ~/.platformio.
FW_DIR = "~/Memphre-Boat-Racing/Real_V1"
PIO_BIN = ""
# git pull --ff-only avant de compiler. Mettre False sans internet (sur
# l'eau) pour flasher le code deja present sur le Pi.
GIT_PULL = True

# Ancien lien UART (pins 7/8), garde pour memoire :
# SERIAL_PORT = "/dev/serial0"

BAUD = 115200               # ignore en USB CDC, sans effet
SCREEN_W, SCREEN_H = 800, 480

# ------------------------------------------------------------ moteurs
ESC_IDS = [10, 11]

# Nombre de PAIRES de poles du moteur (pas le nombre de poles !).
# Sert a convertir l'eRPM du VESC en RPM mecanique : RPM = eRPM / paires.
# Mettre None si le nombre n'est pas connu -> affichage en eRPM brut.
POLE_PAIRS = 6


# ------------------------------------------------------------ commandes
# Envoye au Teensy quand on appuie sur Demarrer. Les gains PID viendront
# s'ajouter ici plus tard (cle "pid"), le transport ne change pas.
START_PAYLOAD = {"cmd": "start"}
STOP_PAYLOAD = {"cmd": "stop"}


# ------------------------------------------------------------ helpers telemetrie
def _escs(d: dict) -> list:
    return d.get("esc", []) or []


def battery_v(d: dict) -> float:
    vals = [e.get("v_in", 0.0) for e in _escs(d)]
    return max(vals) if vals else 0.0


def current_in(d: dict) -> float:
    return sum(e.get("i_in", 0.0) for e in _escs(d))


def power_w(d: dict) -> float:
    return battery_v(d) * current_in(d)


# ------------------------------------------------------------ cartes du haut
@dataclass(frozen=True)
class Metric:
    label: str
    value: Callable[[dict], str]
    unit: str = ""


TOP_METRICS: List[Metric] = [
    Metric("Batterie",  lambda d: "%.1f" % battery_v(d),      "V"),
    Metric("Courant",   lambda d: "%.1f" % current_in(d),     "A"),
    Metric("Puissance", lambda d: "%d"   % round(power_w(d)), "W"),
]

TOP_COLUMNS = 3


# ------------------------------------------------------------ carte par ESC
def esc_value(e: dict) -> str:
    erpm = e.get("erpm", 0)
    if POLE_PAIRS:
        return "%d" % round(erpm / POLE_PAIRS)
    return "%d" % round(erpm)


def esc_unit() -> str:
    return "RPM" if POLE_PAIRS else "eRPM"


# ------------------------------------------------------------ alarmes temperature
# Le pilote ne lit pas de chiffres : chaque temperature affiche OK, ou
# NOT OK (xx °C) des qu'elle atteint sa limite.
# A ajuster selon VESC Tool : par defaut le VESC commence lui-meme a
# limiter le courant vers 85 °C, l'alerte doit donc tomber avant.
T_FET_MAX = 75.0            # °C
T_MOT_MAX = 75.0            # °C

# Hysteresis : une fois en alerte, il faut redescendre de X °C sous la
# limite pour revenir a OK. Evite que l'affichage clignote quand la
# temperature oscille autour du seuil.
TEMP_HYST = 3.0             # °C


@dataclass(frozen=True)
class TempCheck:
    label: str              # texte affiche
    key: str                # cle dans la trame ESC
    limit: float            # °C


ESC_TEMPS: List[TempCheck] = [
    TempCheck("TEMP FET", "t_fet", T_FET_MAX),
    TempCheck("TEMP MOT", "t_mot", T_MOT_MAX),
]


def temp_ok(t: float, limit: float, was_ok: bool = True) -> bool:
    """Seuil a la montee, seuil moins l'hysteresis a la descente."""
    return t < limit if was_ok else t < limit - TEMP_HYST


def temp_text(label: str, t: float, ok: bool) -> str:
    if ok:
        return "%s : OK" % label
    return "%s : NOT OK (%.0f °C)" % (label, t)


def esc_offline_sub(_e: dict) -> str:
    return "aucune trame CAN"


def mode_text(d: dict) -> str:
    return "%s · %s" % (d.get("mode", "?"), d.get("run", "?"))


def target_text(d: dict) -> str:
    """Consigne moteur — affichee dans la barre du haut, pas dans les cartes."""
    return "consigne %d eRPM" % int(d.get("target", 0))
