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
#
# FLASH_MODE = "hex" : Integration taille reel (FreeRTOS, plateforme
#   tsandmann). Le Pi ne peut PAS compiler ce firmware (toolchain
#   tsandmann absente pour ARM) : on flashe FW_HEX, compile sur PC et
#   committe (copie auto par scripts/copie_hex.py).
#   Prerequis Pi : teensy_loader_cli + regles udev PJRC (00-teensy.rules).
# FLASH_MODE = "pio" : ancien mode, compile sur le Pi (Real_V1).
FLASH_MODE = "hex"

# FW_DIR = dossier du firmware (git pull y est lance ; platformio.ini en mode pio).
# PIO_BIN vide = cherche pio dans le PATH puis dans ~/.platformio.
FW_DIR = "~/Memphre-Boat-Racing/TailleReel"  # Integration taille reel
PIO_BIN = ""
FW_HEX = FW_DIR + "/firmware/firmware.hex"
# TEENSY_CLI vide = PATH (apt), puis ~/teensy_loader_cli/teensy_loader_cli
TEENSY_CLI = ""
# git pull --ff-only avant de compiler. Mettre False sans internet (sur
# l'eau) pour flasher le code deja present sur le Pi.
GIT_PULL = True

# Ancien lien UART (pins 7/8), garde pour memoire :
# SERIAL_PORT = "/dev/serial0"

BAUD = 115200               # ignore en USB CDC, sans effet
SCREEN_W, SCREEN_H = 800, 480

# ------------------------------------------------------------ moteurs
# Ordre = ordre d'affichage, de gauche a droite (ESC 11 a gauche).
ESC_IDS = [11, 10]

# Nombre de PAIRES de poles du moteur (pas le nombre de poles !).
# Sert a convertir l'eRPM du VESC en RPM mecanique : RPM = eRPM / paires.
# Mettre None si le nombre n'est pas connu -> affichage en eRPM brut.
POLE_PAIRS = 4


# ------------------------------------------------------------ commandes
# Envoye au Teensy quand on appuie sur Demarrer. Les gains PID viendront
# s'ajouter ici plus tard (cle "pid"), le transport ne change pas.
START_PAYLOAD = {"cmd": "start"}
STOP_PAYLOAD = {"cmd": "stop"}

# Calibration du levier (ecran « Calibrer levier »). Le Teensy n'accepte
# la calibration qu'en IDLE (switch ON a OFF) et sauve min/max en EEPROM.
CALIB_START_PAYLOAD = {"cmd": "calib"}
CALIB_FIN_PAYLOAD = {"cmd": "calib_fin"}
CALIB_ANNULE_PAYLOAD = {"cmd": "calib_annule"}

# Resultat renvoye par le Teensy dans "pil": {"cal_res": n}
CALIB_RESULTATS = {
    0: "",
    1: "Calibration enregistree",
    2: "Refusee : course du levier trop courte",
    3: "Refusee : remettre le levier au repos avant Terminer",
    4: "Calibration annulee",
    5: "Refusee : mettre le switch ON a OFF",
}


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
    TempCheck("FET", "t_fet", T_FET_MAX),       # transistors du VESC
    TempCheck("MOT", "t_mot", T_MOT_MAX),       # moteur
]


def temp_ok(t: float, limit: float, was_ok: bool = True) -> bool:
    """Seuil a la montee, seuil moins l'hysteresis a la descente."""
    return t < limit if was_ok else t < limit - TEMP_HYST


def temp_text(label: str, t: float, ok: bool) -> str:
    if ok:
        return "%s : OK" % label
    return "%s : NOT OK %.0f°C" % (label, t)


def esc_offline_sub(_e: dict) -> str:
    return "aucune trame CAN"


def mode_text(d: dict) -> str:
    return "%s · %s" % (d.get("mode", "?"), d.get("run", "?"))


# Modes affiches en rouge dans la pastille d'etat (ARRET = erreur
# critique detectee par le watchdog du Teensy : moteurs coupes).
MODES_ALERTE = {"ARRET"}


def mode_ok(d: dict) -> bool:
    return d.get("mode") not in MODES_ALERTE


def target_text(d: dict) -> str:
    """Consigne moteur — affichee dans la barre du haut, pas dans les cartes.
    L'unite depend du mode de commande du Teensy (cle "cmode")."""
    v = int(d.get("target", 0))
    mode = d.get("cmode", "ERPM")
    if mode == "COURANT":
        return "consigne %.1f A" % (v / 10.0)
    if mode == "DUTY":
        return "consigne %.1f %%" % (v / 10.0)
    return "consigne %d eRPM" % v


# ------------------------------------------------------------ calibration levier
def _pil(d: dict) -> dict:
    return d.get("pil") or {}


def calib_en_cours(d: dict) -> bool:
    return bool(_pil(d).get("cal"))


def calib_auto(d: dict) -> bool:
    """cal = 2 : lancee par la commande secrete (F/R bascule 5 fois en IDLE)."""
    return _pil(d).get("cal") == 2


def calib_texte_mesure(d: dict) -> str:
    p = _pil(d)
    if "raw" not in p:
        return "levier : --"
    if p.get("cal"):
        return "levier %d   (min %d / max %d)" % (p["raw"], p.get("cmin", 0), p.get("cmax", 0))
    return "levier %d   (bornes en service %d / %d)" % (p["raw"], p.get("lmin", 0), p.get("lmax", 0))


def calib_resultat(d: dict):
    """(texte, ok) ; ok = None si rien a afficher."""
    n = int(_pil(d).get("cal_res", 0))
    return CALIB_RESULTATS.get(n, "resultat %d" % n), (None if n == 0 else n == 1)


# ------------------------------------------------------------ page Xsens
# Cles de la trame du firmware "Integration taille reel" :
#   "imu": {"ok", "frais", "roll", "pitch", "yaw", "vok", "v_kmh"}
#   "gps": {"ok", "lat", "lon", "alt_ok", "alt"}
# Une valeur absente, invalide ou perimee s'affiche "--" : on n'affiche
# jamais au pilote une vitesse ou un angle qui ne sont plus a jour.
SPEED_LABEL = "Vitesse GPS"
SPEED_LABEL_COURT = "Vitesse"      # carte vitesse de la page Moteurs
SPEED_UNIT = "km/h"


def _imu(d: dict) -> dict:
    return d.get("imu") or {}


def _gps(d: dict) -> dict:
    return d.get("gps") or {}


def _imu_fresh(d: dict) -> bool:
    i = _imu(d)
    return bool(i.get("ok")) and bool(i.get("frais", 1))


def speed_text(d: dict) -> str:
    i = _imu(d)
    if i.get("vok") and i.get("frais", 1) and "v_kmh" in i:
        return "%.1f" % i["v_kmh"]
    return "--"


def _angle(d: dict, key: str, fmt: str) -> str:
    return fmt % _imu(d)[key] if _imu_fresh(d) else "--"


# Une ligne par angle affiche a droite de la vitesse.
IMU_METRICS: List[Metric] = [
    Metric("Roulis",  lambda d: _angle(d, "roll",  "%+.1f"), "°"),
    Metric("Tangage", lambda d: _angle(d, "pitch", "%+.1f"), "°"),
    Metric("Lacet",   lambda d: _angle(d, "yaw",   "%.0f"),  "°"),
]


def imu_status(d: dict):
    """(texte, ok) ; ok = None si l'information est absente."""
    if "imu" not in d:
        return "IMU : absent de la trame", None
    i = _imu(d)
    if not i.get("ok"):
        return "IMU : pas de donnees", False
    if not i.get("frais", 1):
        return "IMU : donnees perimees", False
    return "IMU : OK", True


def gps_status(d: dict):
    if "gps" not in d:
        return "GPS : absent de la trame", None
    return ("GPS : OK", True) if _gps(d).get("ok") else ("GPS : pas de position", False)


def position_text(d: dict) -> str:
    g = _gps(d)
    return "%.6f, %.6f" % (g["lat"], g["lon"]) if g.get("ok") else "--"


def altitude_text(d: dict) -> str:
    g = _gps(d)
    return "%.1f m" % g["alt"] if g.get("alt_ok") else "--"
