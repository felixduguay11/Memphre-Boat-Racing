# -*- coding: utf-8 -*-
"""
esc_params.py — verification des parametres des VESC.

Le Teensy lit la config de chaque VESC par CAN (COMM_GET_MCCONF et
COMM_GET_APPCONF) quand les ESC demarrent, et relaie les octets bruts :
    {"type":"escconf","id":10,"k":"mc","t":12345,"ok":1,"len":483,"hex":"..."}
    {"type":"escconf","id":10,"k":"app","t":12345,"ok":0,"err":"timeout"}

Ici on decode (format du firmware VESC 6.06, voir vesc_conf_606.py) et
on compare aux fichiers de reference exportes de VESC Tool :
    esc_params/ESC10_mcconf.h, ESC10_appconf.h, ESC11_mcconf.h, ESC11_appconf.h
Tout est compare, sauf les offsets de calibration (MCCONF_FOC_OFFSETS_CURRENT_*,
_VOLTAGE_*, _VOLTAGE_UNDRIVEN_*) qui changent a chaque calibration.
"""

import json
import math
import os
import re
import struct
import time

from .vesc_conf_606 import (MCCONF_FIELDS, APPCONF_FIELDS,
                            MCCONF_SIGNATURE, APPCONF_SIGNATURE)

KINDS = {
    "mc":  {"fields": MCCONF_FIELDS,  "sig": MCCONF_SIGNATURE,  "fichier": "ESC%d_mcconf.h",
            "nom": "mcconf"},
    "app": {"fields": APPCONF_FIELDS, "sig": APPCONF_SIGNATURE, "fichier": "ESC%d_appconf.h",
            "nom": "appconf"},
}

# Parametres jamais compares (propres a chaque calibration)
EXCLUS = re.compile(r"^MCCONF_FOC_OFFSETS_(CURRENT|VOLTAGE)")

ERREURS_TEENSY = {
    "timeout": "pas de reponse de l'ESC",
    "crc": "reponse corrompue (CRC)",
    "reponse": "reponse inattendue",
}

_INT = {"u8": ">B", "i8": ">b", "u16": ">H", "i16": ">h", "u32": ">I", "i32": ">i"}
_SIZE = {"f32a": 4, "f16": 2, "f32": 4, "u8": 1, "i8": 1, "u16": 2, "i16": 2,
         "u32": 4, "i32": 4}


class ConfError(Exception):
    pass


# ------------------------------------------------------------ decodage
def decode(kind: str, raw: bytes) -> dict:
    """Octets de la config (apres l'octet de commande) -> {define: valeur}."""
    k = KINDS[kind]
    if len(raw) < 4:
        raise ConfError("config trop courte (%d octets)" % len(raw))
    sig = struct.unpack_from(">I", raw, 0)[0]
    if sig != k["sig"]:
        raise ConfError("signature %d au lieu de %d : firmware VESC different de 6.06 ?"
                        % (sig, k["sig"]))
    need = 4 + sum(_SIZE[t] for _, t, _ in k["fields"])
    if len(raw) != need:
        raise ConfError("%d octets au lieu de %d" % (len(raw), need))
    out, i = {}, 4
    for name, typ, scale in k["fields"]:
        if typ == "f32a":
            v = struct.unpack_from(">f", raw, i)[0]
        elif typ == "f16":
            v = struct.unpack_from(">h", raw, i)[0] / scale
        elif typ == "f32":
            v = struct.unpack_from(">i", raw, i)[0] / scale
        else:
            v = struct.unpack_from(_INT[typ], raw, i)[0]
        out[name] = v
        i += _SIZE[typ]
    return out


def encode(kind: str, values: dict) -> bytes:
    """Inverse de decode (simulateur et tests). values : {define: nombre}."""
    k = KINDS[kind]
    b = bytearray(struct.pack(">I", k["sig"]))
    for name, typ, scale in k["fields"]:
        v = float(values.get(name, 0))
        if typ == "f32a":
            b += struct.pack(">f", v)
        elif typ == "f16":
            b += struct.pack(">h", int(v * scale))          # tronque, comme le VESC
        elif typ == "f32":
            b += struct.pack(">i", int(v * scale))
        else:
            b += struct.pack(_INT[typ], int(v))
    return bytes(b)


# ------------------------------------------------------------ references
_DEFINE = re.compile(r"^\s*#define\s+([A-Z][A-Z0-9_]*)\s+(\S+)")
_COMMENT = re.compile(r"^\s*//\s*(.+?)\s*$")


def lire_reference(path: str) -> dict:
    """Fichier .h de VESC Tool -> {define: (valeur texte, libelle)}."""
    out, label = {}, ""
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = _DEFINE.match(line)
            if m:
                out[m.group(1)] = (m.group(2), label)
                label = ""
                continue
            m = _COMMENT.match(line)
            if m:
                label = m.group(1)
    return out


def _nombre(txt: str):
    try:
        return float(txt)
    except ValueError:
        return None


def _egal(typ: str, scale, voulu: float, reel: float) -> bool:
    if typ in _INT:
        # Entiers (parfois un float tronque par le VESC, ex. temperatures)
        return abs(voulu - reel) < 1.0
    # VESC Tool exporte ~6 chiffres significatifs
    tol = 1e-5 * max(abs(voulu), abs(reel))
    if scale:
        tol += 1.0001 / scale                     # pas du codage, troncature
    return abs(voulu - reel) <= tol + 1e-12


def formater(v) -> str:
    if v is None:
        return "?"
    if isinstance(v, int):
        return str(v)
    if math.isfinite(v) and v == int(v) and abs(v) < 1e7:
        return str(int(v))
    return "%.6g" % v


def comparer(kind: str, valeurs: dict, reference: dict) -> list:
    """-> [{"nom", "libelle", "voulu", "reel"}] pour chaque parametre different."""
    diffs = []
    for name, typ, scale in KINDS[kind]["fields"]:
        if EXCLUS.match(name) or name not in reference:
            continue
        txt, label = reference[name]
        voulu = _nombre(txt)
        reel = valeurs[name]
        if voulu is None or not _egal(typ, scale, voulu, reel):
            diffs.append({"nom": name, "libelle": label or name,
                          "voulu": txt if voulu is None else formater(voulu),
                          "reel": formater(reel)})
    return diffs


# ------------------------------------------------------------ verificateur
class EscParamsChecker:
    """Traite les messages "escconf" du Teensy.

    traiter(msg) retourne None si le message a deja ete traite (simple
    renvoi d'une lecture deja vue), sinon un resultat :
        {"id", "k", "nom", "erreur": str|None, "diffs": [...]}
    Chaque lecture est aussi enregistree dans <log_dir>/esc_params/.
    """

    def __init__(self, ref_dir: str, log_dir: str = None):
        self.ref_dir = os.path.expanduser(ref_dir)
        self.log_dir = os.path.join(os.path.expanduser(log_dir), "esc_params") if log_dir else None
        self._vus = {}                      # (id, k) -> t de la derniere lecture traitee

    def fichier_reference(self, esc_id: int, kind: str) -> str:
        return os.path.join(self.ref_dir, KINDS[kind]["fichier"] % esc_id)

    def traiter(self, msg: dict):
        try:
            esc_id, kind = int(msg["id"]), msg["k"]
        except (KeyError, TypeError, ValueError):
            return None
        if kind not in KINDS:
            return None
        cle = (esc_id, kind)
        t = msg.get("t")
        if t is not None and self._vus.get(cle) == t:
            return None                     # renvoi d'une lecture deja traitee
        self._vus[cle] = t

        res = {"id": esc_id, "k": kind, "nom": KINDS[kind]["nom"], "erreur": None,
               "diffs": [], "valeurs": None}
        if not msg.get("ok"):
            err = msg.get("err", "")
            res["erreur"] = "lecture impossible : " + ERREURS_TEENSY.get(err, err or "?")
        else:
            try:
                res["valeurs"] = decode(kind, bytes.fromhex(msg.get("hex", "")))
            except (ConfError, ValueError) as e:
                res["erreur"] = "decodage impossible : %s" % e
        if res["valeurs"] is not None:
            path = self.fichier_reference(esc_id, kind)
            try:
                res["diffs"] = comparer(kind, res["valeurs"], lire_reference(path))
            except OSError:
                res["erreur"] = "fichier de reference absent : esc_params/%s" % os.path.basename(path)
        self._journaliser(msg, res)
        return res

    def _journaliser(self, msg: dict, res: dict):
        if not self.log_dir:
            return
        try:
            os.makedirs(self.log_dir, exist_ok=True)
            nom = "%s_ESC%d_%s.json" % (time.strftime("%Y%m%d-%H%M%S"), res["id"], res["nom"])
            rec = {"heure": time.strftime("%Y-%m-%d %H:%M:%S"), "id": res["id"],
                   "config": res["nom"], "t_teensy": msg.get("t"), "erreur": res["erreur"],
                   "differences": res["diffs"], "valeurs": res["valeurs"],
                   "hex": msg.get("hex")}
            with open(os.path.join(self.log_dir, nom), "w", encoding="utf-8") as f:
                json.dump(rec, f, indent=1)
        except OSError as e:
            print("[esc_params] journal impossible:", e)
