# -*- coding: utf-8 -*-
"""
config.py — tout ce qui se regle sans toucher au code de l'UI.

Ajouter une metrique = ajouter une ligne dans TOP_METRICS (carte du haut de la page Moteurs).
Ajouter un ESC = ajouter son id dans ESC_IDS.
"""

import os
from dataclasses import dataclass
from typing import Callable, List, Optional

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


def courant_moteur(d: dict) -> float:
    """Courant moteur (couple) le plus grand des ESC qui repondent, en A."""
    vals = [abs(e.get("i_mot", 0.0)) for e in _escs(d) if e.get("ok", 1)]
    return max(vals) if vals else 0.0


# Courant moteur affiche en rouge a partir de cette valeur : la limite
# Motor Current Max des VESC est 300 A (saturation = le moteur ne peut
# pas donner plus de couple).
I_MOT_SATURE_A = 290


# ------------------------------------------------------------ cartes du haut
@dataclass(frozen=True)
class Metric:
    label: str
    value: Callable[[dict], str]
    unit: str = ""
    alerte: Optional[Callable[[dict], bool]] = None   # True -> valeur en rouge


TOP_METRICS: List[Metric] = [
    Metric("Courant moteur", lambda d: "%d" % round(courant_moteur(d)), "A",
           alerte=lambda d: courant_moteur(d) >= I_MOT_SATURE_A),
    Metric("Puissance",      lambda d: "%d" % round(power_w(d)),        "W"),
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
# Avec le firmware actuel, c'est le Teensy qui decide (config.h :
# T_*_ATTENTION, T_*_MAX) et envoie le niveau de chaque capteur dans
# esc[].tn = [FET, MOT] : voir temp_statut() / temp_alerte() plus bas.
# Les seuils ci-dessous ne servent plus qu'avec un ancien firmware sans
# "tn" : chaque temperature affiche OK, ou NOT OK (xx °C) a sa limite.
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


# Protection de temperature du Teensy : niveau par capteur (esc[].tn),
# seuils / plafond / plafond applique dans "th" (task_rpi.cpp).
TEMP_OK, TEMP_ATTENTION, TEMP_LIMITE, TEMP_CAPTEUR = 0, 1, 2, 3


def temp_niveau(e: dict, n: int):
    """Niveau du capteur n (0 FET, 1 MOT, ordre de ESC_TEMPS) envoye par
    le Teensy, ou None (ancien firmware)."""
    tn = e.get("tn")
    return tn[n] if isinstance(tn, list) and len(tn) > n else None


def temp_statut(label: str, t, niveau: int):
    """(texte, etat) pour la carte ESC ; etat = True (vert), "warn"
    (orange), False (rouge)."""
    if niveau == TEMP_CAPTEUR:
        return "%s : capteur ?" % label, "warn"
    if niveau == TEMP_LIMITE:
        return "%s : LIMITE %.0f°C" % (label, t), False
    if niveau == TEMP_ATTENTION:
        return "%s : %.0f°C" % (label, t), "warn"
    return "%s : OK" % label, True


def temp_alerte(d: dict):
    """Bandeau d'avertissement sous la barre du haut (commune a toutes
    les pages) : (texte, niveau) du pire capteur de tous les ESC, ou
    None. Ordre : limite > attention > capteur invalide."""
    rangs = {TEMP_LIMITE: 3, TEMP_ATTENTION: 2, TEMP_CAPTEUR: 1}
    noms = {"FET": "VESC", "MOT": "MOTEUR"}
    pire = None
    for e in d.get("esc") or []:
        for n, chk in enumerate(ESC_TEMPS):
            niv = temp_niveau(e, n)
            if niv not in rangs:
                continue
            if pire is None or rangs[niv] > rangs[pire[0]]:
                pire = (niv, "%s %s" % (noms.get(chk.label, chk.label), e.get("id", "?")),
                        e.get(chk.key))
    if pire is None:
        return None
    niv, nom, t = pire
    if niv == TEMP_CAPTEUR:
        return "%s : capteur de temperature invalide" % nom, niv
    tt = "%.0f °C" % t if isinstance(t, (int, float)) else "? °C"
    if niv == TEMP_LIMITE:
        th = d.get("th") or {}
        if th.get("on"):
            return "%s TROP CHAUD %s · vitesse limitee" % (nom, tt), niv
        return "%s TROP CHAUD %s" % (nom, tt), niv
    return "%s chaud %s" % (nom, tt), niv


# ------------------------------------------------------------ batterie secondaire 4S
# Mesuree par le Teensy (pin 25, diviseur) : "b12": {"v": tension,
# "n": niveau, "a": seuil d'attention, "s": seuil d'arret, "d": secondes
# avant l'arret}. Seuils et delai dans config.h (V_BATT_SEC_*).
B12_OK, B12_ATTENTION, B12_ARRET, B12_INVALIDE = 0, 1, 2, 3


def batt12_text(d: dict) -> str:
    """Tension de la batterie 12V pour la page BMS ("--" si absente ou invalide)."""
    b = d.get("b12") or {}
    if "v" not in b or b.get("n") == B12_INVALIDE:
        return "--"
    return "%.1f" % b["v"]


def batt12_alerte(d: dict):
    """(gravite, texte, rouge) pour le bandeau, ou None."""
    b = d.get("b12") or {}
    n = b.get("n", B12_OK)
    v = b.get("v", 0.0)
    if n == B12_ARRET:
        return 6, "BATTERIE 12V VIDE %.1f V · ARRET, redemarrer le Teensy" % v, True
    if n == B12_ATTENTION and b.get("d", 0) > 0:
        return 5, "BATTERIE 12V %.1f V · ARRET DANS %d s" % (v, b["d"]), True
    if n == B12_ATTENTION:
        return 3, "Batterie 12V faible %.1f V" % v, False
    if n == B12_INVALIDE:
        return 1, "Batterie 12V : mesure invalide", False
    return None


def bandeau_alerte(d: dict):
    """Bandeau sous la barre du haut : (texte, rouge) de l'alerte la plus
    grave entre la batterie 12V et les temperatures, ou None.
    Gravite : 12V vide > arret 12V imminent > temperature limite >
    12V faible > temperature attention > mesure ou capteur invalide."""
    alertes = []
    b = batt12_alerte(d)
    if b:
        alertes.append(b)
    t = temp_alerte(d)
    if t:
        texte, niv = t
        gravite = {TEMP_LIMITE: 4, TEMP_ATTENTION: 2}.get(niv, 1)
        alertes.append((gravite, texte, niv == TEMP_LIMITE))
    if not alertes:
        return None
    _, texte, rouge = max(alertes, key=lambda a: a[0])
    return texte, rouge


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


def target_short(d: dict) -> str:
    """Consigne sans le mot « consigne » (carte Vitesse de la page Moteurs).
    Pendant une calibration du levier : la mesure du levier a la place."""
    if calib_en_cours(d):
        return "CALIB %s" % _pil(d).get("raw", "--")
    if "target" not in d:
        return "--"
    return target_text(d).replace("consigne ", "")


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


# ------------------------------------------------------------ calibration des servos
# Bouton Calibration de l'accueil -> pop-up : Levier ou un des 3 servos.
# Le Teensy (servos.cpp) commande le servo choisi en µs ; + / - le
# deplacent, Enregistrer sauve la position comme Min / Neutre / Max en
# EEPROM (gardee au redemarrage et apres un reflash).
# Trame : "sv": {"c": servo en calibration (-1 aucun), "us": sa position,
#   "r": dernier resultat, "k": derniere valeur enregistree (0 min,
#   1 neutre, 2 max), "cal": [[min, neutre, max, valide] x 3]} en µs,
#   0 = pas enregistre. Ordre : 0 avant, 1 arriere gauche, 2 arriere droit.
SERVO_NOMS = ["avant", "arrière gauche", "arrière droit"]
SERVO_CLES = ["av", "ag", "ad"]          # cles de foils.cmd, meme ordre
SERVO_PAS_FIN = 1                        # µs, boutons - / +
SERVO_PAS_GROS = 10                      # µs, boutons -- / ++
SERVO_US_CENTRE = 1500                   # centre du servo (reglage d'usine)
SERVO_US_PAR_DEG = 10.0                  # 10 µs = 1° au palonnier (SERVO_US_PAR_DEG de config.h)
SERVO_VALEURS = [("min", "Min"), ("neutre", "Neutre"), ("max", "Max")]   # ordre de "cal"
SERVO_FIN_PAYLOAD = {"cmd": "servo_fin"}
# Signe de vie tant que l'ecran de calibration est ouvert : sans lui
# pendant SERVO_CALIB_VIE_MS (config.h, 1 s), le Teensy arrete la calibration.
SERVO_VIE_PAYLOAD = {"cmd": "servo_vie"}
SERVO_VIE_MS = 200


def servo_calib_payload(i: int) -> dict:
    return {"cmd": "servo_calib", "s": i}


def servo_pas_payload(d: int) -> dict:
    return {"cmd": "servo_pas", "d": d}


def servo_enr_payload(k: str) -> dict:
    return {"cmd": "servo_enr", "k": k}


def _sv(d: dict) -> dict:
    return d.get("sv") or {}


def servo_calib_actif(d: dict) -> int:
    """Servo en calibration (0..2), -1 si aucun."""
    try:
        return int(_sv(d).get("c", -1))
    except (TypeError, ValueError):
        return -1


def servo_deg(us: float) -> float:
    """Angle du palonnier par rapport au centre du servo (1500 µs)."""
    return (us - SERVO_US_CENTRE) / SERVO_US_PAR_DEG


def servo_cal(d: dict, i: int):
    """(min, neutre, max, valide) en µs (0 = pas enregistre), ou None."""
    cal = _sv(d).get("cal") or []
    if i >= len(cal) or len(cal[i]) < 4:
        return None
    mn, neu, mx, ok = cal[i][:4]
    return int(mn), int(neu), int(mx), bool(ok)


def servo_pos_text(d: dict, i: int) -> str:
    if servo_calib_actif(d) != i or "us" not in _sv(d):
        return "-- µs"
    return "%d µs" % _sv(d)["us"]


def servo_pos_deg_text(d: dict, i: int) -> str:
    if servo_calib_actif(d) != i or "us" not in _sv(d):
        return ""
    return "%+.1f° (palonnier, depuis le centre du servo)" % servo_deg(_sv(d)["us"])


def servo_valeur_text(d: dict, i: int, k: int) -> str:
    c = servo_cal(d, i)
    return "%d µs" % c[k] if c and c[k] else "--"


SERVO_FINS = {
    2: "Refusée : couper le switch ON",
    4: "Arrêtée : le switch ON a été activé",
    5: "Arrêtée : le Teensy ne recevait plus l'écran",
}


def servo_calib_message(d: dict, i: int):
    """(texte, ok) sous les boutons ; ok = None : neutre."""
    sv = _sv(d)
    if not sv:
        return "Pas de données servos dans la trame (firmware à jour ?)", False
    r = int(sv.get("r", 0))
    if servo_calib_actif(d) != i:
        return SERVO_FINS.get(r, "En attente du Teensy..."), (False if r in SERVO_FINS else None)
    c = servo_cal(d, i)
    if c is None:
        return "", None
    prefixe = ""
    if r == 1:
        k = int(sv.get("k", 0))
        if 0 <= k < 3 and c[k]:
            prefixe = "%s enregistré : %d µs. " % (SERVO_VALEURS[k][1], c[k])
    manque = [lab for n, (_, lab) in enumerate(SERVO_VALEURS) if not c[n]]
    if manque:
        return prefixe + "À enregistrer : " + ", ".join(manque), None
    if not c[3]:
        return prefixe + "Il faut Min < Neutre < Max", False
    return prefixe + "Calibration valide", True


# ------------------------------------------------------------ cruise control
# Case « Cruise control » en bas a droite de la page Moteurs :
#   cruise arrete -> pop-up a 3 choix : ERPM / vitesse GPS / Annuler
#   cruise actif  -> arret du cruise (la telemetrie continue ; le Teensy
#                    met la consigne a 0 jusqu'au levier au repos)
# Etat RUN·CRUISE de la machine d'etat du Teensy : il n'accepte la
# demande qu'en RUN·FORWARD (levier pousse) et coupe le cruise si le
# levier revient au repos.
# Les cibles (4600 eRPM, 15 km/h) se reglent dans config.h du Teensy
# (CRUISE_ERPM, CRUISE_VITESSE_KMH) ; il les renvoie dans la trame :
#   "cr": {"m": 0 off / 1 ERPM / 2 vitesse, "t": duree en ms,
#          "fin": raison de sortie, "erpm": cible, "kmh": cible}
CRUISE_OFF, CRUISE_ERPM, CRUISE_VITESSE = 0, 1, 2

CRUISE_PAYLOADS = {
    CRUISE_ERPM:    {"cmd": "cruise_erpm"},
    CRUISE_VITESSE: {"cmd": "cruise_vitesse"},
}
CRUISE_OFF_PAYLOAD = {"cmd": "cruise_off"}

# Signe de vie, envoye en continu tant que la telemetrie est affichee.
# Sans lui pendant CRUISE_UI_TIMEOUT_MS (config.h, 1 s), le Teensy sort
# du cruise : ecran fige, UI fermee ou USB debranche.
CRUISE_VIE_PAYLOAD = {"cmd": "cruise_vie"}
CRUISE_VIE_MS = 200

# Le pop-up de choix se ferme seul apres ce temps (ou si le bateau
# n'est plus en RUN·FORWARD).
CRUISE_CHOIX_S = 15

# Raison de la derniere sortie / du dernier refus ("cr": {"fin": n}),
# memes numeros que CRUISE_FIN_* du Teensy (task_state_machine.h).
CRUISE_FINS = {
    0: "",
    1: "arrete a l'ecran",
    2: "levier au repos",
    3: "switch F/R",
    4: "vitesse GPS perdue",
    5: "ecran muet",
    6: "switch ON a OFF",
    7: "erreur critique",
    8: "refuse : levier pas en FORWARD",
    9: "refuse : pas de vitesse GPS",
}
CRUISE_REFUS = (8, 9)
CRUISE_REFUS_AFFICHE_S = 4.0    # un refus reste affiche ce temps apres le choix
# Sorties apres lesquelles le Teensy tient la consigne a 0 jusqu'au
# levier au repos (en IDLE / ARRET, le rearmement est deja affiche).
CRUISE_SORTIES_REARME = (1, 2, 3, 4, 5)
CRUISE_TEXTE_REARME = "ramener le levier au repos"

# Etats (cle "run") depuis lesquels le Teensy accepte le cruise.
CRUISE_RUN_OK = {"FORWARD", "CONTROLE"}


def _cr(d: dict) -> dict:
    return d.get("cr") or {}


def cruise_mode(d: dict) -> int:
    return int(_cr(d).get("m", 0))


def cruise_possible(d: dict, mode: int = CRUISE_ERPM) -> bool:
    """Le choix sert a quelque chose : firmware avec cruise, RUN·FORWARD,
    et une vitesse GPS valide pour le cruise vitesse."""
    if "cr" not in d or d.get("mode") != "RUN" or d.get("run") not in CRUISE_RUN_OK:
        return False
    return mode != CRUISE_VITESSE or speed_text(d) != "--"


def cruise_cible(d: dict, mode: int) -> str:
    """Cible du mode, telle que reglee dans config.h du Teensy."""
    c = _cr(d)
    if mode == CRUISE_ERPM:
        return "%d eRPM" % c["erpm"] if "erpm" in c else "-- eRPM"
    return "%g km/h" % c["kmh"] if "kmh" in c else "-- km/h"


def cruise_bouton_texte(d: dict) -> str:
    """Texte de la case : « Cruise control », ou le mode actif."""
    m = cruise_mode(d)
    return "Cruise control" if m == CRUISE_OFF else "ON · " + cruise_cible(d, m)


def cruise_timer_text(d: dict) -> str:
    """Duree de l'activation en min:s (figee apres la sortie)."""
    c = _cr(d)
    if "t" not in c:
        return "--:--"
    s = int(c["t"]) // 1000
    return "%d:%02d" % (s // 60, s % 60)


def cruise_fin(d: dict) -> int:
    return int(_cr(d).get("fin", 0))


def cruise_fin_text(d: dict) -> str:
    n = cruise_fin(d)
    return CRUISE_FINS.get(n, "fin %d" % n)


def cruise_attend_rearme(d: dict) -> bool:
    """Apres une sortie du cruise : consigne a 0 tant que le levier
    n'est pas revenu au repos (cle "arme" du Teensy)."""
    return (cruise_mode(d) == CRUISE_OFF and cruise_fin(d) in CRUISE_SORTIES_REARME
            and d.get("mode") == "RUN" and not int(d.get("arme", 1)))


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


# ------------------------------------------------------------ page Foils (sonars + servos)
# Cles de la trame du firmware "Integration taille reel" (task_rpi.cpp) :
#   "son":   {"av", "ag", "ad"}  distance des sonars, cm     (SONARS_ACTIFS)
#   "sio":   {"av", "ag", "ad"}  sortie TOR, 1 = mesure valide
#   "foils": {"on", "cmd": {av, ag, ad}, "h": {av, ag, ad}, "p", "r"}  (FOILS_ACTIFS)
#            on = 1 en CONTROLE ; cmd = commande envoyee au servo (µs, 0 = pas commande) ;
#            h = sortie du PID de hauteur, p / r = sorties PID pitch / roll.
# Un bloc absent (option a 0 dans config.h) s'affiche "--" et
# "absent de la trame" : jamais une valeur inventee.
FOILS_PAGE_LABEL = "Foils"

@dataclass(frozen=True)
class FoilChannel:
    key: str            # cle dans son / sio / foils.cmd
    label: str          # titre de la carte


# Ordre d'affichage, de gauche a droite (vue de l'arriere du bateau)
FOIL_CHANNELS: List[FoilChannel] = [
    FoilChannel("ag", "Arr. gauche"),
    FoilChannel("av", "Avant"),
    FoilChannel("ad", "Arr. droit"),
]

# Min / Neutre / Max de chaque servo : calibration envoyee par le Teensy ("sv").
SONAR_UNIT = "cm"


def _son(d: dict) -> dict:
    return d.get("son") or {}


def _foils(d: dict) -> dict:
    return d.get("foils") or {}


def sonar_text(d: dict, key: str) -> str:
    v = _son(d).get(key)
    return "--" if v is None else "%.0f" % v


def sonar_status(d: dict, key: str):
    """(texte, ok) ; ok = None si l'information est absente."""
    if "son" not in d:
        return "Sonar : absent", None
    io = (d.get("sio") or {}).get(key)
    if io is None:
        return "Sonar : --", None
    return ("Sonar : OK", True) if io else ("Sonar : NON VALIDE", False)


def servo_value(d: dict, key: str):
    """Commande du servo en µs, ou None (absente ou servo pas commande)."""
    v = (_foils(d).get("cmd") or {}).get(key)
    return v if v else None


def _servo_cal_cle(d: dict, key: str):
    return servo_cal(d, SERVO_CLES.index(key)) if key in SERVO_CLES else None


def servo_text(d: dict, key: str) -> str:
    c = _servo_cal_cle(d, key)
    if c is not None and not c[3]:
        return "Servo : non calibré"
    v = servo_value(d, key)
    if v is None:
        return "Servo : --"
    if c is None:
        return "Servo %d µs" % v
    return "Servo %d µs  (%+.1f°)" % (v, (v - c[1]) / SERVO_US_PAR_DEG)


def servo_pct(d: dict, key: str):
    """Position de la commande entre Min (0) et Max (100), ou None."""
    v = servo_value(d, key)
    c = _servo_cal_cle(d, key)
    if v is None or c is None or not c[3]:
        return None
    lo, hi = c[0], c[2]
    return max(0.0, min(100.0, (v - lo) * 100.0 / (hi - lo))) if hi > lo else None


def foils_status(d: dict):
    """(texte, ok) pour la ligne du bas de la page Foils."""
    if "foils" not in d:
        return "Foils : absent de la trame", None
    if _foils(d).get("on"):
        return "Foils : CONTROLE actif", True
    return "Foils : servos au neutre", None


def foils_info(d: dict) -> str:
    """Sorties des PID pitch / roll (la hauteur se voit sur chaque servo)."""
    f = _foils(d)
    if not f:
        return "sonars seuls" if "son" in d else "--"
    parts = ["%s %+.1f" % (lab, f[k] + 0.0) for k, lab in (("p", "pitch"), ("r", "roll"))
             if f.get(k) is not None]
    return ("PID " + "   ".join(parts)) if parts else "--"


# ------------------------------------------------------------ BMS (Bluetooth)
# Le Pi lit le BMS ANT de la batterie directement en BLE (ui/bms.py).
# L'app ANT BMS du telephone doit etre FERMEE : une seule connexion a la fois.
BMS_ENABLED = True
# Debut du nom Bluetooth (ANT-BLE16ZMUB, ANT@BLE22AAUB... -> "ANT").
# Plus sur une fois connu : BMS_ADDRESS = "AA:BB:CC:DD:EE:FF" (ignore le nom).
BMS_NAME_PREFIX = "ANT"
BMS_ADDRESS = "32:7F:4D:74:87:EB"   # ANT-BLE24CBUB-0812 (notre batterie)

# Deux batteries en meme temps (futures, une par BMS) : decommenter les
# deux lignes BMS_ADDRESSES / BMS_MONTAGE. BMS_ADDRESS est alors ignore.
# Le Pi se connecte aux deux BMS en parallele ; l'UI affiche le pire des
# deux (charge la plus basse, temperature la plus haute, cellule la plus
# basse) et le plafond 20 % part de la charge la plus basse.
#BMS_ADDRESSES = []
#BMS_ADDRESSES = ["87:85:96:56:B1:2E",    # ANT@BLE16ZNUB-3KM7 (batterie 1)
#                 "84:66:2F:2E:86:38"]    # ANT@BLE16ZNUB-CF4J (batterie 2)
# Branchement des deux packs : "parallele" (tensions egales, courants
# additionnes) ou "serie" (tensions additionnees, meme courant).
BMS_MONTAGE = "parallele"
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
# Temperature batterie : affichee "TEMP : OK" / "NOT OK xx°C" sur la page
# Moteurs, comme les ESC (meme hysteresis TEMP_HYST : retour a OK sous
# BMS_TEMP_MAX - TEMP_HYST).
# Max du pack : 54 °C. L'alerte tombe 4 °C avant pour laisser au pilote
# le temps de reduire avant d'atteindre la limite.
BMS_TEMP_LIMITE = 54.0      # °C, maximum du pack (info)
BMS_TEMP_MAX = 50.0         # °C, seuil d'alerte
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
    Metric("Batterie 12V", batt12_text, "V"),      # mesuree par le Teensy (pas le BMS)
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


def bms_combine(samples: list, montage: str = None) -> dict:
    """Plusieurs packs -> une mesure. Securite d'abord : charge la plus
    basse, temperature la plus haute, toutes les cellules ensemble."""
    montage = montage or BMS_MONTAGE
    if len(samples) == 1:
        return dict(samples[0])

    def vals(k):
        return [s[k] for s in samples if s.get(k) is not None]

    out = {"packs": [{"soc": s.get("battery_level"), "v": s.get("voltage"),
                      "i": s.get("current")} for s in samples]}
    v, i = vals("voltage"), vals("current")
    if v:
        out["voltage"] = sum(v) if montage == "serie" else sum(v) / len(v)
    if i:
        out["current"] = sum(i) / len(i) if montage == "serie" else sum(i)
    if vals("power"):
        out["power"] = sum(vals("power"))
    if vals("battery_level"):
        out["battery_level"] = min(vals("battery_level"))
    if vals("cycle_charge"):
        c = vals("cycle_charge")
        out["cycle_charge"] = min(c) if montage == "serie" else sum(c)
    cells = [c for s in samples for c in (s.get("cell_voltages") or [])]
    if cells:
        out["cell_voltages"] = cells
        out["delta_voltage"] = max(cells) - min(cells)
    temps = [t for s in samples for t in (s.get("temp_values") or [])]
    if temps:
        out["temp_values"] = temps
    if vals("temperature"):
        out["temperature"] = max(vals("temperature"))
    for k in ("chrg_mosfet", "dischrg_mosfet"):
        if vals(k):
            out[k] = all(vals(k))
    out["problem"] = any(vals("problem"))
    out["problem_code"] = 0
    for c in vals("problem_code"):
        out["problem_code"] |= int(c)
    return out


def bms_info(d: dict) -> str:
    b = bms(d)
    if not b:
        return "--"
    parts = ["P%d %s %%" % (n + 1, "--" if p.get("soc") is None else p["soc"])
             for n, p in enumerate(b.get("packs") or [])]
    if b.get("delta_voltage") is not None:
        parts.append("ecart %d mV" % round(b["delta_voltage"] * 1000))
    if b.get("power") is not None:
        parts.append("%d W" % round(b["power"]))
    if b.get("cycle_charge") is not None:
        parts.append("%.1f Ah" % b["cycle_charge"])
    return "   ".join(parts) or "--"
