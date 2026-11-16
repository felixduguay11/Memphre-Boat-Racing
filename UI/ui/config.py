# -*- coding: utf-8 -*-
"""
config.py — tout ce qui se regle sans toucher au code de l'UI.

Ajouter une metrique = ajouter une ligne dans TOP_METRICS (carte V | A | W).
Ajouter un ESC = ajouter son id dans ESC_IDS.
"""

import os
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
# Ordre = ordre d'affichage, de gauche a droite (ESC 10 a gauche).
ESC_IDS = [10, 11]

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
# Parametres des VESC : le Teensy les lit par CAN quand les ESC demarrent
# et les envoie ({"type":"escconf",...}). L'UI les compare aux fichiers
# de reference du dossier ESC_PARAMS_DIR (ESC10_mcconf.h, ESC10_appconf.h,
# ESC11_mcconf.h, ESC11_appconf.h, exportes de VESC Tool) et affiche un
# pop-up des ecarts. ESCCONF_PAYLOAD redemande les configs deja lues.
ESC_PARAMS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                              "esc_params")
ESCCONF_PAYLOAD = {"cmd": "escconf"}
ESC_PARAMS_POPUP_S = 20

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

TOP_COLUMNS = 3              # ancien affichage (une carte par valeur)

# Rangee du haut de la page Moteurs : TOP_METRICS dans une seule carte
# (moitie gauche) et la charge du BMS en barre (moitie droite).
CHARGE_LABEL = "Charge"


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


# ------------------------------------------------------------ BMS (Bluetooth)
# Le Pi lit le BMS ANT de la batterie directement en BLE (ui/bms.py).
# L'app ANT BMS du telephone doit etre FERMEE : une seule connexion a la fois.
BMS_ENABLED = True
# Debut du nom Bluetooth (ANT-BLE16ZMUB, ANT@BLE22AAUB... -> "ANT").
# Plus sur une fois connu : BMS_ADDRESS = "AA:BB:CC:DD:EE:FF" (ignore le nom).
BMS_NAME_PREFIX = "ANT"
BMS_ADDRESS = "32:7F:4D:74:87:EB"   # ANT-BLE24CBUB-0812 (notre batterie)
# "" = detection auto ; sinon "ant_bms" (recent) ou "ant_leg_bms" (ancien)
BMS_TYPE = ""
BMS_SECRET = ""             # mot de passe BLE si le BMS en a un, sinon vide
BMS_PERIOD_S = 1.0          # une lecture par seconde suffit
BMS_STALE_S = 5.0           # au-dela : "--" a l'ecran, jamais une vieille valeur

# Alarmes BMS (a ajuster selon la chimie du pack)
# Charge batterie (% du BMS) :
#   > BMS_WARN_SOC          barre verte
#   <= BMS_WARN_SOC         barre jaune + "Attention" en rouge
#   <= BMS_CAP_SOC          barre rouge + vitesse plafonnee : le Pi envoie
#                           BATT_BASSE_PAYLOAD et le Teensy limite la
#                           consigne a BATT_BASSE_PCT % du max (config.h).
# Le plafond n'est demande qu'apres BMS_CAP_CONFIRM mesures de suite sous
# le seuil (une lecture BLE aberrante ne bride pas le bateau), puis il est
# renvoye a chaque mesure (le Teensy l'oublie s'il redemarre). Il est
# leve (BATT_OK_PAYLOAD) quand la charge remonte a BMS_CAP_SOC +
# BMS_CAP_REARM (batterie changee ou rechargee).
# NB : {"cmd":"stop"} n'arrete PAS les moteurs (seulement la telemetrie).
BMS_WARN_SOC = 30           # %
BMS_CAP_SOC = 20            # %
BMS_CAP_CONFIRM = 2         # mesures consecutives (1 mesure / BMS_PERIOD_S)
BMS_CAP_REARM = 3           # %
BATT_BASSE_PCT = 20         # meme valeur que BATT_BASSE_PCT de config.h (affichage)
BATT_BASSE_PAYLOAD = {"cmd": "batt_basse"}
BATT_OK_PAYLOAD = {"cmd": "batt_ok"}
CHARGE_WARN_TEXT = "Attention"
CHARGE_CAP_TEXT = "Vitesse limitee"
BMS_CELL_MIN_V = 3.30       # V, Li-ion NMC 18S (2.90 pour du LiFePO4)
BMS_DELTA_MAX_V = 0.10      # V, ecart max entre cellules
# Temperature batterie : affichee "TEMP : OK" / "TEMP : NOT OK xx°C" sur
# la page Moteurs, comme les ESC (meme hysteresis TEMP_HYST).
# Li-ion NMC : la plupart des fiches de cellules limitent la decharge a
# 60 °C ; l'alerte tombe avant. Verifier la fiche des cellules du pack et
# la protection haute temperature reglee dans l'app ANT BMS.
BMS_TEMP_MAX = 55.0         # °C
BMS_TEMP_LABEL = "TEMP"


def bms_temp_text(t: float, ok: bool) -> str:
    """Comme temp_text des ESC, en plus court quand ce n'est pas OK
    (la place est partagee avec "Attention" / "Limite 20 %")."""
    return "%s : OK" % BMS_TEMP_LABEL if ok else "NOT OK %.0f°C" % t


def bms_log_record(sample: dict) -> dict:
    """Mesure BMS telle qu'ecrite dans le log de session.
    Cellules et sondes en dict ({"1": 3.71, ...}) : l'outil analyse/
    les eclate en colonnes bms.cell.1, bms.cell.2... tracables."""
    rec = {k: v for k, v in sample.items() if k not in ("cell_voltages", "temp_values")}
    cells = sample.get("cell_voltages") or []
    temps = sample.get("temp_values") or []
    if cells:
        rec["cell"] = {str(i + 1): v for i, v in enumerate(cells)}
        rec["cell_min"] = min(cells)
        rec["cell_max"] = max(cells)
    if temps:
        rec["temp"] = {str(i + 1): v for i, v in enumerate(temps)}
        rec["temp_max"] = max(temps)
    return rec


def bat_cap(d: dict):
    """Plafond batterie confirme par le Teensy : True / False, ou None si
    la trame ne contient pas la cle (ancien firmware ou pas de trame)."""
    return None if "bat_cap" not in d else bool(d["bat_cap"])


def bms_temp(d: dict):
    """Temperature batterie la plus haute (BMS frais), ou None."""
    b = bms(d)
    t = b.get("temp_values") or []
    if t:
        return max(t)
    return b.get("temperature")


def bms_soc(d: dict):
    """Charge en % (BMS frais), ou None."""
    return bms(d).get("battery_level")


def bms(d: dict) -> dict:
    """Mesure BMS fraiche, ou {} si absente / perimee."""
    b = d.get("bms") or {}
    return b if b.get("ok") else {}


def _cells(d: dict) -> list:
    return bms(d).get("cell_voltages") or []


def _bms_fmt(key: str, fmt: str):
    def f(d):
        v = bms(d).get(key)
        return fmt % v if v is not None else "--"
    return f


def bms_cell_min(d):
    c = _cells(d)
    return "%.3f" % min(c) if c else "--"


def bms_cell_max(d):
    c = _cells(d)
    return "%.3f" % max(c) if c else "--"


def bms_temp_max(d):
    t = bms(d).get("temp_values") or []
    return "%.0f" % max(t) if t else _bms_fmt("temperature", "%.0f")(d)


# Grande carte a gauche
BMS_SOC = Metric("Charge batterie", _bms_fmt("battery_level", "%d"), "%")

# Une ligne par valeur a droite
BMS_METRICS: List[Metric] = [
    Metric("Tension",   _bms_fmt("voltage", "%.1f"), "V"),
    Metric("Courant",   _bms_fmt("current", "%+.1f"), "A"),
    Metric("Cell min",  bms_cell_min, "V"),
    Metric("Cell max",  bms_cell_max, "V"),
    Metric("Temp max",  bms_temp_max, "°C"),
]


def bms_status(d: dict):
    """(texte, ok) pour la ligne du bas ; ok = None si pas de donnee."""
    b = d.get("bms")
    if not b:
        return "BMS : pas de lien", None
    if not b.get("ok"):
        return "BMS : donnees perimees", False
    alarmes = []
    soc = b.get("battery_level")
    if soc is not None and soc <= BMS_CAP_SOC:
        alarmes.append("vitesse limitee")
    elif soc is not None and soc <= BMS_WARN_SOC:
        alarmes.append("charge basse")
    c = b.get("cell_voltages") or []
    if c and min(c) < BMS_CELL_MIN_V:
        alarmes.append("cellule basse")
    dv = b.get("delta_voltage")
    if dv is not None and dv > BMS_DELTA_MAX_V:
        alarmes.append("desequilibre")
    t = b.get("temp_values") or []
    if t and max(t) >= BMS_TEMP_MAX:
        alarmes.append("chaud")
    if b.get("dischrg_mosfet") is False:
        alarmes.append("decharge coupee")
    if b.get("problem") or b.get("problem_code"):
        alarmes.append("defaut %s" % b.get("problem_code", ""))
    if alarmes:
        return "BMS : " + ", ".join(alarmes), False
    return "BMS : OK", True


def bms_info(d: dict) -> str:
    b = bms(d)
    if not b:
        return "--"
    parts = []
    if b.get("delta_voltage") is not None:
        parts.append("ecart %d mV" % round(b["delta_voltage"] * 1000))
    if b.get("power") is not None:
        parts.append("%d W" % round(b["power"]))
    if b.get("cycle_charge") is not None:
        parts.append("%.1f Ah" % b["cycle_charge"])
    return "   ".join(parts) or "--"
