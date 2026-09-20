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

# Ancien lien UART (pins 7/8), garde pour memoire :
# SERIAL_PORT = "/dev/serial0"

BAUD = 115200               # ignore en USB CDC, sans effet
LINK_TIMEOUT_S = 1.0        # sans trame depuis X s -> lien considere perdu
SCREEN_W, SCREEN_H = 800, 480

# ------------------------------------------------------------ moteurs
ESC_IDS = [10, 11]

# Nombre de PAIRES de poles du moteur (pas le nombre de poles !).
# Sert a convertir l'eRPM du VESC en RPM mecanique : RPM = eRPM / paires.
# Mettre None si le nombre n'est pas connu -> affichage en eRPM brut.
POLE_PAIRS = None


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


def esc_sub(e: dict) -> str:
    return "%s  ·  %.1f A  ·  FET %.0f °C  ·  mot %.0f °C" % (
        esc_unit(), e.get("i_mot", 0.0), e.get("t_fet", 0.0), e.get("t_mot", 0.0))


def esc_offline_sub(_e: dict) -> str:
    return "aucune trame CAN"


def mode_text(d: dict) -> str:
    return "%s · %s" % (d.get("mode", "?"), d.get("run", "?"))


def target_text(d: dict) -> str:
    """Consigne moteur — affichee dans la barre du haut, pas dans les cartes."""
    return "consigne %d eRPM" % int(d.get("target", 0))
