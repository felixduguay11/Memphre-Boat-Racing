#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
analyse_logs.py — graphiques et résumé d'une session du bateau
(logs JSONL de l'UI : ~/memphre_logs/session_XXXX_AAAAMMJJ-HHMMSS.jsonl).

Utilisation (sur le PC, avec matplotlib et numpy) :
    python analyse_logs.py session_0024_20261023-142126.jsonl
    python analyse_logs.py C:\\Users\\feldu\\memphre_logs          # prend la session la plus récente
    python analyse_logs.py session.jsonl --debut 1700 --fin 1900    # zoom (secondes depuis le début du log)
    python analyse_logs.py session.jsonl --show                     # ouvre aussi les fenêtres

Produit, dans <session>_graphs/ (à côté du log, ou --out) :
    1_conduite.png      levier, consigne vs régime réel, vitesse GPS
    2_puissance.png     courant moteur, courant batterie, tension, puissance
    3_thermique.png     température FET / moteur, échauffement (I²)
    4_correlations.png  vitesse vs régime, puissance vs vitesse, courant vs régime, duty vs régime
    5_sante.png         bus CAN, watchdog, attitude Xsens
    6_moteur.png        régime, courant, puissance mécanique, rendement de chaque moteur
    resume.txt          chiffres clés (max, énergie, temps au plafond de courant, erreurs)

Les zones grisées = mode RUN. Régime en tr/min = |eRPM| / POLE_PAIRS.
Les périodes inactives de plus d'une minute (moteurs arrêtés, bateau immobile,
hors RUN) sont retirées des graphiques temporels : un trait gris pointillé
marque chaque coupure, avec la durée retirée. L'axe garde le temps réel.
--complet garde tout. Le résumé est toujours calculé sur le log complet.
"""

import argparse
import glob
import json
import os
import sys
import warnings

import numpy as np
import matplotlib

POLE_PAIRS = 4                      # moteur : 8 pôles = 4 paires (comme l'UI)

# Moteur Flipsky 90165, fiche 90-090165-130-000 (KV 130, essai à 60 V)
MOT_KT_FICHE = 0.0791               # constante de couple de la fiche (N·m par A continu)
# Le VESC (FOC) donne l'amplitude du courant de phase, pas un courant continu :
# couple = 1,5 × pp × flux × iq = Kt_continu × √3/2 × iq  →  environ 0,0685 N·m/A
MOT_KT = MOT_KT_FICHE * np.sqrt(3) / 2
MOT_I0 = 5.5                        # courant à vide (A) : ne produit pas de couple utile
MOT_P_NOM = 8000                    # puissance nominale annoncée (W)
ESC_RENDEMENT = 0.97                # pertes de l'ESC estimées (puissance moteur = 97 % de l'entrée)
# Courbes de la fiche (essai à tension fixe de 60 V, modèle linéaire)
FICHE_V, FICHE_N0, FICHE_I0 = 60.0, 8600.0, 5.5          # V, tr/min à vide, A à vide
FICHE_T_BLOC, FICHE_I_BLOC = 40.9524, 518.0              # couple (N·m) et courant de blocage


def rendement_fiche(couple):
    """Rendement lu sur la fiche (60 V) pour un couple donné (N·m), en %."""
    t = np.clip(couple, 0, FICHE_T_BLOC)
    n = FICHE_N0 * (1 - t / FICHE_T_BLOC)
    i = FICHE_I0 + (FICHE_I_BLOC - FICHE_I0) * t / FICHE_T_BLOC
    return 100 * t * n * 2 * np.pi / 60 / (FICHE_V * i)

# Couleurs (palette catégorielle validée : bleu, orange, aqua) + encres neutres
C_ESC10 = "#2a78d6"
C_ESC11 = "#eb6834"
C_TROIS = "#1baf7a"                 # 3e série (consigne, total…)
C_ENCRE = "#0b0b0b"
C_ENCRE2 = "#52514e"
C_GRILLE = "#e4e3df"
C_RUN = "#efeeea"                   # fond des périodes RUN
C_ALERTE = "#e34948"                # réservé aux événements (ARRET, erreurs)


# ------------------------------------------------------------ lecture
def trouver_log(chemin: str) -> str:
    if os.path.isdir(chemin):
        logs = sorted(glob.glob(os.path.join(chemin, "session_*.jsonl")), key=os.path.getmtime)
        if not logs:
            sys.exit("aucun session_*.jsonl dans " + chemin)
        return logs[-1]
    return chemin


def lire(path: str):
    rows = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            try:
                d = json.loads(line)
            except ValueError:
                continue                       # ligne coupée (coupure de courant)
            if "esc" in d and "t_pi" in d:     # trames de télémétrie seulement
                rows.append(d)
    if not rows:
        sys.exit("aucune trame de télémétrie dans " + path)
    return rows


def col(rows, fn, nan=np.nan):
    out = np.empty(len(rows))
    for i, r in enumerate(rows):
        try:
            v = fn(r)
            out[i] = nan if v is None else float(v)
        except (KeyError, IndexError, TypeError, ValueError):
            out[i] = nan
    return out


def esc(rows, k, key):
    """Valeur d'un ESC (k = 0 → ESC 10, 1 → ESC 11), NaN si l'ESC est muet."""
    return col(rows, lambda r: r["esc"][k][key] if r["esc"][k].get("ok", 1) else None)


def moyenne_nan(x, n):
    """Moyenne glissante sur n échantillons qui ignore les NaN."""
    ok = np.isfinite(x)
    k = np.ones(n)
    somme = np.convolve(np.where(ok, x, 0.0), k, mode="same")
    nb = np.convolve(ok.astype(float), k, mode="same")
    with np.errstate(all="ignore"):
        return np.where(nb > n / 2, somme / nb, np.nan)


def charger(path, debut=None, fin=None):
    rows = lire(path)
    t = col(rows, lambda r: r["t_pi"])
    m = np.ones(len(rows), bool)
    if debut is not None:
        m &= t >= debut
    if fin is not None:
        m &= t <= fin
    rows = [r for r, ok in zip(rows, m) if ok]
    t = t[m]

    D = {"t_s": t, "t": (t - t[0]) / 60.0 if debut is None else t / 60.0}
    D["mode"] = np.array([r.get("mode", "") for r in rows])
    D["run"] = np.array([r.get("run", "") for r in rows])
    D["cmode"] = rows[-1].get("cmode", "ERPM")
    D["lev"] = col(rows, lambda r: r["pil"]["lev"]) / 1023 * 100
    D["target"] = col(rows, lambda r: r["target"])
    D["ramped"] = col(rows, lambda r: r["ramped"])
    for k, n in ((0, "10"), (1, "11")):
        D["ok" + n] = col(rows, lambda r, k=k: r["esc"][k].get("ok", 1))
        D["rpm" + n] = np.abs(esc(rows, k, "erpm")) / POLE_PAIRS
        D["duty" + n] = np.abs(esc(rows, k, "duty"))
        D["imot" + n] = np.abs(esc(rows, k, "i_mot"))
        D["iin" + n] = esc(rows, k, "i_in")
        D["vin" + n] = esc(rows, k, "v_in")
        D["tfet" + n] = esc(rows, k, "t_fet")
        tm = esc(rows, k, "t_mot")
        tm[tm < -50] = np.nan                          # capteur non branché (-99 °C)
        D["tmot" + n] = tm
    D["escpin"] = col(rows, lambda r: r["pil"]["esc"])
    # Valeur de pil.esc quand les ESC sont alimentés : 1 depuis octobre, 0 dans les
    # anciens logs (logique inversée). On prend la valeur la plus fréquente en RUN.
    en_run = (D["mode"] == "RUN") & np.isfinite(D["escpin"])
    esc_on = 1 if not en_run.any() else round(float(np.median(D["escpin"][en_run])))
    vin = np.fmax(D["vin10"], D["vin11"])
    vin[(vin < 20) | (D["escpin"] != esc_on)] = np.nan  # ESC éteints : condensateurs qui se vident
    D["vbat"] = vin
    D["ibat"] = np.nansum([D["iin10"], D["iin11"]], axis=0)
    D["pbat"] = D["vbat"] * D["ibat"]
    # Moteur : couple = Kt × (I − I0), puissance mécanique = couple × ω.
    # Rendement moteur = puissance mécanique / puissance électrique reçue par le
    # moteur (entrée de l'ESC × ESC_RENDEMENT), moyennes sur 1 s, si l'ESC fournit > 300 W.
    # Rendement « fiche » = ce que la fiche donne pour le même couple, à 60 V.
    for n in ("10", "11"):
        couple = MOT_KT * np.clip(D["imot" + n] - MOT_I0, 0, None)
        pmec = couple * D["rpm" + n] * 2 * np.pi / 60
        pin = D["vin" + n] * D["iin" + n]
        pmec_m, pin_m = moyenne_nan(pmec, 20), moyenne_nan(pin, 20)
        with np.errstate(all="ignore"):
            eta = pmec_m / (pin_m * ESC_RENDEMENT) * 100
        valide = (pin_m > 300) & (eta <= 100)
        eta[~valide] = np.nan
        eta_f = rendement_fiche(moyenne_nan(couple, 20))
        eta_f[~valide] = np.nan
        D["couple" + n], D["pmec" + n], D["pin" + n] = couple, pmec, pin
        D["eta" + n], D["etaf" + n] = eta, eta_f
    D["v_kmh"] = col(rows, lambda r: r["imu"]["v_kmh"] if r["imu"].get("vok", 1) else None)
    D["v_kmh"][(D["v_kmh"] < 0) | (D["v_kmh"] > 80)] = np.nan   # valeurs aberrantes (anciens logs)
    D["roll"] = col(rows, lambda r: r["imu"]["roll"])
    D["pitch"] = col(rows, lambda r: r["imu"]["pitch"])
    D["can_rx"] = col(rows, lambda r: r["can"]["rx"])
    D["can_pct"] = col(rows, lambda r: r["can"]["pct"])
    D["can_tec"] = col(rows, lambda r: r["can"]["tec"])
    D["can_flt"] = col(rows, lambda r: r["can"]["flt"])
    D["wd_err"] = col(rows, lambda r: r["wd"]["err"])
    D["prej"] = col(rows, lambda r: r["gps"]["prej"])
    D["vrej"] = col(rows, lambda r: r["imu"]["vrej"])
    return D


# ------------------------------------------------------------ coupure des temps morts
INACTIF_MIN_S = 60       # on ne coupe que les temps morts plus longs que ça
MARGE_S = 15             # contexte gardé avant et après chaque période active


def actif(D):
    """True quand il se passe quelque chose : RUN, moteur qui tourne, courant,
    bateau qui avance, ou passage en ARRET."""
    with np.errstate(invalid="ignore"):
        a = D["mode"] == "RUN"
        for n in ("10", "11"):
            a |= np.nan_to_num(D["rpm" + n]) > 30
            a |= np.nan_to_num(D["imot" + n]) > 5
        a |= np.nan_to_num(D["v_kmh"]) > 2
    a |= (D["mode"] == "ARRET") & np.roll(D["mode"] != "ARRET", 1)
    return a


def compacter(D):
    """Retire les temps morts des données de graphique.

    D["t"] devient un temps « compressé » (min) ; D["t_reel"] et D["coupures"]
    servent à afficher le temps réel et à marquer les coupures. Une ligne
    vide (NaN) est insérée à chaque coupure pour que les courbes ne relient
    pas les deux côtés."""
    t = D["t_s"]
    N = len(t)
    a = actif(D)
    if not a.any():
        return D
    # Étendre chaque période active de MARGE_S de chaque côté
    idx = np.flatnonzero(a)
    debut = np.searchsorted(t, t[idx] - MARGE_S)
    fin = np.searchsorted(t, t[idx] + MARGE_S, side="right")
    d = np.zeros(N + 1, int)
    np.add.at(d, debut, 1)
    np.add.at(d, fin, -1)
    garde = np.cumsum(d[:-1]) > 0
    # Remettre les petits trous (< INACTIF_MIN_S)
    k = np.flatnonzero(garde)
    for i0, i1 in zip(k[:-1], k[1:]):
        if i1 > i0 + 1 and t[i1] - t[i0] < INACTIF_MIN_S:
            garde[i0:i1] = True
    k = np.flatnonzero(garde)
    if len(k) == N:
        return D

    # Segments gardés
    sauts = np.flatnonzero(np.diff(k) > 1)
    segs = np.split(k, sauts + 1)
    t_min = D["t"]                                # temps réel (min)
    total = sum(t_min[s[-1]] - t_min[s[0]] for s in segs)
    ecart = max(0.05, 0.02 * total)               # largeur visuelle d'une coupure (min)

    lignes, t_c, t_r, coupures = [], [], [], []
    x = 0.0
    for j, s in enumerate(segs):
        if j:
            retire = t_min[s[0]] - t_min[segs[j - 1][-1]]
            coupures.append((x + ecart / 2, retire))
            lignes.append(-1)                     # ligne vide
            t_c.append(x + ecart / 2)
            t_r.append(np.nan)
            x += ecart
        x0 = t_min[s[0]]
        lignes.extend(s.tolist())
        t_c.extend((x + t_min[s] - x0).tolist())
        t_r.extend(t_min[s].tolist())
        x += t_min[s[-1]] - x0
    lignes = np.array(lignes)
    vide = lignes < 0
    src = np.where(vide, np.maximum.accumulate(np.where(vide, 0, lignes)), lignes)

    C = {}
    for cle, v in D.items():
        if isinstance(v, np.ndarray) and len(v) == N:
            w = v[src].copy()
            if w.dtype.kind == "f":
                w[vide] = np.nan
            C[cle] = w
        else:
            C[cle] = v
    C["t"] = np.array(t_c)
    C["t_reel"] = np.array(t_r)
    C["coupures"] = coupures
    C["retire_min"] = sum(r for _, r in coupures)
    return C


def axe_temps_reel(ax, D):
    """Graduations rondes en temps réel sur l'axe compressé + marques de coupure."""
    import matplotlib.ticker as mt
    if "t_reel" not in D:
        return
    tc, tr = D["t"], D["t_reel"]
    ok = np.isfinite(tr)
    # Segments continus (séparés par les lignes vides)
    coupe = np.flatnonzero(~ok)
    bornes = [0] + coupe.tolist() + [len(tr)]
    segs = [(a + (a > 0), b) for a, b in zip(bornes[:-1], bornes[1:])]
    segs = [(a, b) for a, b in segs if b - a > 0]
    long_c = tc[-1] - tc[0]
    pas = next((p for p in (0.25, 0.5, 1, 2, 5, 10, 15, 30, 60) if long_c / p <= 10), 120)
    pos, lab = [], []
    for a, b in segs:
        r0, r1 = tr[a], tr[b - 1]
        rs = np.arange(np.ceil(r0 / pas) * pas, r1 + 1e-9, pas)
        if not len(rs):                           # segment court : graduer son début
            rs = [r0]
        for r in rs:
            pos.append(tc[a] + (r - r0))
            lab.append(("%.1f" % r).rstrip("0").rstrip("."))
    ax.xaxis.set_major_locator(mt.FixedLocator(pos))
    ax.xaxis.set_major_formatter(mt.FixedFormatter(lab))
    for xc, _ in D["coupures"]:
        ax.axvline(xc, color=C_ENCRE2, lw=0.9, ls=(0, (2, 2)), zorder=1)


def etiqueter_coupures(ax, D):
    """Durée retirée, écrite le long de chaque trait de coupure."""
    for xc, retire in D.get("coupures", []):
        txt = "−%.0f min" % retire if retire >= 1.5 else "−%.0f s" % (retire * 60)
        ax.text(xc, 0.5, txt, transform=ax.get_xaxis_transform(), rotation=90,
                ha="center", va="center", fontsize=7, color=C_ENCRE2, zorder=5,
                bbox=dict(boxstyle="square,pad=0.15", fc="white", ec="none"))


# ------------------------------------------------------------ mise en forme
def style():
    import matplotlib.pyplot as plt
    plt.rcParams.update({
        "figure.dpi": 110, "savefig.dpi": 130, "font.size": 9,
        "axes.edgecolor": C_GRILLE, "axes.labelcolor": C_ENCRE2, "axes.titlesize": 10,
        "axes.titleweight": "bold", "axes.titlecolor": C_ENCRE, "axes.titlelocation": "left",
        "axes.grid": True, "grid.color": C_GRILLE, "grid.linewidth": 0.6,
        "xtick.color": C_ENCRE2, "ytick.color": C_ENCRE2,
        "axes.spines.top": False, "axes.spines.right": False,
        "lines.linewidth": 1.1, "legend.fontsize": 8, "legend.frameon": True,
        "legend.framealpha": 0.9, "legend.edgecolor": "none", "legend.facecolor": "white",
    })
    return plt


def zones_run(ax, D):
    """Fond gris pendant RUN, traits rouges aux passages en ARRET."""
    run = D["mode"] == "RUN"
    t = D["t"]
    i = 0
    while i < len(t):
        if run[i]:
            j = i
            while j + 1 < len(t) and run[j + 1]:
                j += 1
            ax.axvspan(t[i], t[j], color=C_RUN, lw=0, zorder=0)
            i = j + 1
        else:
            i += 1
    arret = np.flatnonzero((D["mode"] == "ARRET") & np.roll(D["mode"] != "ARRET", 1))
    for k in arret:
        ax.axvline(t[k], color=C_ALERTE, lw=1, ls="--", zorder=1)


def page(plt, n, titre, D, hauteur=2.0):
    fig, axs = plt.subplots(n, 1, figsize=(12, hauteur * n + 0.8), sharex=True)
    axs = np.atleast_1d(axs)
    fig.suptitle(titre, x=0.01, ha="left", fontsize=12, fontweight="bold", color=C_ENCRE)
    for ax in axs:
        zones_run(ax, D)
        axe_temps_reel(ax, D)
        etiqueter_coupures(ax, D)
    if D.get("coupures"):
        axs[-1].set_xlabel("temps réel (min) — temps morts retirés (%.0f min, traits pointillés)"
                           % D["retire_min"])
    else:
        axs[-1].set_xlabel("temps (min)")
    return fig, axs


def deux_esc(ax, D, cle, label, unite):
    ax.plot(D["t"], D[cle + "10"], color=C_ESC10, label="ESC 10")
    ax.plot(D["t"], D[cle + "11"], color=C_ESC11, label="ESC 11")
    ax.set_title(label)
    ax.set_ylabel(unite)
    ax.legend(loc="upper left", ncol=2)


def roulant(x, n):
    x = np.nan_to_num(x)
    return np.convolve(x, np.ones(n) / n, mode="same")


# ------------------------------------------------------------ pages
def p_conduite(plt, D, nom):
    fig, ax = page(plt, 3, "Conduite — " + nom, D)
    ax[0].plot(D["t"], D["lev"], color=C_ENCRE2)
    ax[0].set_title("Levier (0 = repos, 100 = à fond)")
    ax[0].set_ylabel("%")
    ax[0].set_ylim(-3, 103)

    if D["cmode"] == "ERPM":
        ax[1].plot(D["t"], np.abs(D["ramped"]) / POLE_PAIRS, color=C_TROIS, lw=1.6,
                   label="consigne envoyée (rampée)")
    ax[1].plot(D["t"], D["rpm10"], color=C_ESC10, label="ESC 10 réel")
    ax[1].plot(D["t"], D["rpm11"], color=C_ESC11, label="ESC 11 réel")
    ax[1].set_title("Régime moteur : consigne vs réel (écart = moteur qui ne suit pas)")
    ax[1].set_ylabel("tr/min")
    ax[1].legend(loc="upper left", ncol=3)

    ax[2].plot(D["t"], D["v_kmh"], color=C_ENCRE)
    ax[2].set_title("Vitesse GPS")
    ax[2].set_ylabel("km/h")
    return fig


def p_puissance(plt, D, nom):
    fig, ax = page(plt, 4, "Puissance — " + nom, D)
    deux_esc(ax[0], D, "imot", "Courant moteur (couple) — plateau = limite Motor Current Max", "A")
    deux_esc(ax[1], D, "iin", "Courant batterie par ESC", "A")
    ax[1].plot(D["t"], D["ibat"], color=C_TROIS, lw=1.4, label="total")
    ax[1].legend(loc="upper left", ncol=3)
    ax[2].plot(D["t"], D["vbat"], color=C_ENCRE)
    ax[2].set_title("Tension batterie (creux = chute sous charge)")
    ax[2].set_ylabel("V")
    ax[3].plot(D["t"], D["pbat"] / 1000, color=C_ENCRE)
    ax[3].set_title("Puissance batterie totale (les deux ESC)")
    ax[3].set_ylabel("kW")
    return fig


def p_thermique(plt, D, nom):
    n_mot = np.isfinite(D["tmot10"]).any() or np.isfinite(D["tmot11"]).any()
    fig, ax = page(plt, 3 if n_mot else 2, "Thermique — " + nom, D)
    deux_esc(ax[0], D, "tfet", "Température des MOSFET (ESC)", "°C")
    i = 1
    if n_mot:
        deux_esc(ax[1], D, "tmot", "Température moteur", "°C")
        i = 2
    w = 20 * 10                                         # 10 s à 20 Hz
    ax[i].plot(D["t"], roulant(D["imot10"] ** 2, w) / 1000, color=C_ESC10, label="ESC 10")
    ax[i].plot(D["t"], roulant(D["imot11"] ** 2, w) / 1000, color=C_ESC11, label="ESC 11")
    ax[i].set_title("Échauffement : I² moteur moyen sur 10 s (chaleur ∝ I²) — courbes égales = même charge")
    ax[i].set_ylabel("kA²")
    ax[i].legend(loc="upper left", ncol=2)
    return fig


def p_correlations(plt, D, nom):
    fig, ax = plt.subplots(2, 2, figsize=(12, 8.5))
    fig.suptitle("Points de fonctionnement (mode RUN) — " + nom, x=0.01, ha="left",
                 fontsize=12, fontweight="bold", color=C_ENCRE)
    run = D["mode"] == "RUN"
    with np.errstate(all="ignore"):
        rpm = np.nanmean(np.vstack([D["rpm10"], D["rpm11"]]), axis=0)
    kw = dict(s=6, alpha=0.35, linewidths=0)

    a = ax[0, 0]
    a.scatter(rpm[run], D["v_kmh"][run], color=C_ENCRE2, **kw)
    a.set_title("Vitesse GPS vs régime moyen")
    a.set_xlabel("tr/min"); a.set_ylabel("km/h")

    a = ax[0, 1]
    a.scatter(D["v_kmh"][run], D["pbat"][run] / 1000, color=C_ENCRE2, **kw)
    a.set_title("Puissance batterie vs vitesse (coût de la vitesse)")
    a.set_xlabel("km/h"); a.set_ylabel("kW")

    a = ax[1, 0]
    a.scatter(D["rpm10"][run], D["imot10"][run], color=C_ESC10, label="ESC 10", **kw)
    a.scatter(D["rpm11"][run], D["imot11"][run], color=C_ESC11, label="ESC 11", **kw)
    a.set_title("Courant moteur vs régime (courbe d'hélice ; mur horizontal = limite de courant)")
    a.set_xlabel("tr/min"); a.set_ylabel("A")
    a.legend(loc="lower right", markerscale=3)

    a = ax[1, 1]
    a.scatter(D["rpm10"][run], D["duty10"][run], color=C_ESC10, label="ESC 10", **kw)
    a.scatter(D["rpm11"][run], D["duty11"][run], color=C_ESC11, label="ESC 11", **kw)
    a.axhline(0.95, color=C_ENCRE2, lw=0.8, ls="--")
    a.text(0, 0.955, " duty max VESC", color=C_ENCRE2, va="bottom", fontsize=8)
    a.set_title("Duty vs régime (loin de 0,95 = marge de tension)")
    a.set_xlabel("tr/min"); a.set_ylabel("duty")
    a.legend(loc="center left", markerscale=3)
    for a in ax.flat:
        a.grid(True)
    fig.tight_layout(rect=(0, 0, 1, 0.96))
    return fig


def p_sante(plt, D, nom):
    fig, ax = page(plt, 4, "Santé du système — " + nom, D, hauteur=1.8)
    dt = np.gradient(D["t_s"])
    dt[dt <= 0] = np.nan
    rate = np.gradient(D["can_rx"]) / dt
    ax[0].plot(D["t"], rate, color=C_ENCRE)
    ax[0].set_title("Bus CAN : trames reçues par seconde (0 = plus de communication avec les ESC)")
    ax[0].set_ylabel("trames/s")

    ax[1].step(D["t"], np.minimum(D["can_flt"], 2), color=C_ALERTE, where="post")
    ax[1].set_yticks([0, 1, 2])
    ax[1].set_yticklabels(["actif (OK)", "passif", "bus-off"])
    ax[1].set_ylim(-0.3, 2.3)
    ax[1].set_title("Bus CAN : état d'erreur du Teensy (bus-off normal quand les ESC sont éteints)")

    ax[2].plot(D["t"], D["roll"], color=C_ESC10, label="roulis")
    ax[2].plot(D["t"], D["pitch"], color=C_ESC11, label="tangage")
    ax[2].set_title("Attitude Xsens")
    ax[2].set_ylabel("°")
    ax[2].legend(loc="upper left", ncol=2)

    ax[3].step(D["t"], D["wd_err"], color=C_ALERTE, where="post")
    ax[3].set_title("Watchdog : code d'erreur (0 = OK ; 65536 / 131072 / 196608 = VESC 10 / 11 / les deux muets)")
    return fig


def p_moteur(plt, D, nom):
    """Comme la fiche du moteur, mais en fonction du temps : une échelle par
    grandeur (un graphique chacune), les deux moteurs superposés."""
    fig, ax = page(plt, 4, "Moteurs (Flipsky 90165, KV 130) — %s" % nom, D)
    deux_esc(ax[0], D, "rpm", "Régime", "tr/min")
    deux_esc(ax[1], D, "imot", "Courant moteur (couple = %.1f mN·m/A × (I − %.1f A))"
             % (MOT_KT * 1000, MOT_I0), "A")
    ax[2].plot(D["t"], D["pmec10"] / 1000, color=C_ESC10, label="ESC 10")
    ax[2].plot(D["t"], D["pmec11"] / 1000, color=C_ESC11, label="ESC 11")
    ax[2].axhline(MOT_P_NOM / 1000, color=C_ENCRE2, lw=0.9, ls="--")
    ax[2].text(1.0, MOT_P_NOM / 1000, "nominal %.0f kW " % (MOT_P_NOM / 1000),
               transform=ax[2].get_yaxis_transform(), ha="right", va="bottom",
               fontsize=8, color=C_ENCRE2)
    pmax = np.nanmax(np.r_[D["pmec10"], D["pmec11"], [0]]) / 1000
    ax[2].set_ylim(0, max(pmax, MOT_P_NOM / 1000) * 1.12)
    ax[2].set_title("Puissance mécanique à l'arbre (couple × ω)")
    ax[2].set_ylabel("kW")
    ax[2].legend(loc="upper left", ncol=2)
    a = ax[3]
    a.plot(D["t"], D["eta10"], color=C_ESC10, label="ESC 10 mesuré")
    a.plot(D["t"], D["eta11"], color=C_ESC11, label="ESC 11 mesuré")
    a.plot(D["t"], D["etaf10"], color=C_ESC10, lw=0.9, ls=(0, (3, 2)), label="ESC 10 fiche (60 V)")
    a.plot(D["t"], D["etaf11"], color=C_ESC11, lw=0.9, ls=(0, (3, 2)), label="ESC 11 fiche (60 V)")
    a.set_title("Rendement moteur : mesuré (P méca / P élec. reçue, moy. 1 s) "
                "et fiche au même couple (pointillé)")
    a.set_ylabel("%")
    a.set_ylim(0, 100)
    a.legend(loc="lower left", ncol=4)
    return fig


# ------------------------------------------------------------ résumé
def resume(D, path):
    t, run = D["t_s"], D["mode"] == "RUN"
    dt = np.clip(np.diff(t, prepend=t[0]), 0, 1.0)      # trous > 1 s ignorés
    L = []
    p = L.append
    p("Session : %s" % os.path.basename(path))
    p("Durée du log : %.1f min, dont RUN : %.1f min" % ((t[-1] - t[0]) / 60, dt[run].sum() / 60))
    p("Mode de commande : %s" % D["cmode"])
    p("")
    p("Vitesse GPS max : %.1f km/h" % np.nanmax(D["v_kmh"]))
    for n in ("10", "11"):
        p("ESC %s : régime max %4.0f tr/min | courant moteur max %5.1f A | batterie max %5.1f A | "
          "duty max %.2f | FET max %4.1f °C" % (
              n, np.nanmax(D["rpm" + n]), np.nanmax(D["imot" + n]), np.nanmax(D["iin" + n]),
              np.nanmax(D["duty" + n]), np.nanmax(D["tfet" + n])))
        if np.isfinite(D["tmot" + n]).any():
            p("         moteur max %.1f °C" % np.nanmax(D["tmot" + n]))
    vr = D["vbat"][run]
    if np.isfinite(vr).any():
        p("Tension batterie en RUN : %.1f V max, %.1f V min (chute sous charge %.1f V)" % (
            np.nanmax(vr), np.nanmin(vr), np.nanmax(vr) - np.nanmin(vr)))
    p("Puissance batterie max : %.1f kW" % (np.nanmax(D["pbat"]) / 1000))
    wh = np.nansum(D["pbat"] * dt) / 3600
    p("Énergie consommée (ESC) : %.0f Wh" % wh)
    for n in ("10", "11"):
        pm, pi_ = D["pmec" + n], D["pin" + n]
        m = run & np.isfinite(pm) & np.isfinite(pi_) & (pi_ > 300)
        if m.any():
            ef = D["etaf" + n]
            mf = m & np.isfinite(ef)
            p("Moteur ESC %s : puissance mécanique max %.1f kW, couple max %.1f N·m, "
              "rendement moteur moyen %.0f %% (fiche au même couple : %.0f %%)" % (
                  n, np.nanmax(pm) / 1000, np.nanmax(D["couple" + n]),
                  100 * np.sum(pm[m] * dt[m]) / np.sum(ESC_RENDEMENT * pi_[m] * dt[m]),
                  np.sum(ef[mf] * pi_[mf] * dt[mf]) / np.sum(pi_[mf] * dt[mf]) if mf.any() else np.nan))
    p("")

    # Plafond de courant : moteur loin derrière sa consigne alors que le courant est au max
    if D["cmode"] == "ERPM":
        for n in ("10", "11"):
            imax = np.nanmax(D["imot" + n])
            cons = np.abs(D["ramped"]) / POLE_PAIRS
            sat = run & (D["imot" + n] > 0.9 * imax) & (cons > 1.2 * D["rpm" + n] + 100)
            p("ESC %s : %.0f s au plafond de courant (≥ 90 %% de %.0f A et régime < consigne)" % (
                n, dt[sat].sum(), imax))
    p("")

    # Communication et erreurs
    muet = {n: dt[(D["ok" + n] == 0) & run].sum() for n in ("10", "11")}
    p("ESC muet pendant RUN : ESC 10 %.1f s, ESC 11 %.1f s" % (muet["10"], muet["11"]))
    if np.isfinite(D["can_rx"]).any():
        p("Bus CAN : %d trames reçues, bus-off pendant %.1f s en RUN, %% mauvaises max %.2f %%" % (
            np.nanmax(D["can_rx"]) - np.nanmin(D["can_rx"]), dt[(D["can_flt"] >= 2) & run].sum(),
            np.nanmax(D["can_pct"]) if np.isfinite(D["can_pct"]).any() else 0))
    else:
        p("Bus CAN : pas de données (log d'une version plus ancienne du firmware)")
    arret = np.flatnonzero((D["mode"] == "ARRET") & np.roll(D["mode"] != "ARRET", 1))
    p("Passages en ARRET (erreur critique) : %d %s" % (
        len(arret), ", ".join("à %.1f min" % D["t"][k] for k in arret[:10])))
    codes = sorted(set(int(x) for x in D["wd_err"][np.isfinite(D["wd_err"])] if x))
    p("Codes watchdog vus : %s" % (codes or "aucun"))
    p("Rejets GPS (vitesse / position) : %d / %d" % (
        np.nanmax(D["vrej"]) if np.isfinite(D["vrej"]).any() else 0,
        np.nanmax(D["prej"]) if np.isfinite(D["prej"]).any() else 0))
    return "\n".join(L)


# ------------------------------------------------------------ main
def main():
    ap = argparse.ArgumentParser(description="Graphiques d'une session du bateau")
    ap.add_argument("log", help="fichier session_*.jsonl, ou dossier (prend le plus récent)")
    ap.add_argument("--out", help="dossier de sortie (défaut : <session>_graphs à côté du log)")
    ap.add_argument("--debut", type=float, help="début du zoom (s depuis le début du log)")
    ap.add_argument("--fin", type=float, help="fin du zoom (s)")
    ap.add_argument("--show", action="store_true", help="ouvrir aussi les fenêtres")
    ap.add_argument("--complet", action="store_true",
                    help="garder les temps morts dans les graphiques")
    a = ap.parse_args()
    # Anciens logs : champs absents (CAN, GPS...) → colonnes vides, sans avertissements
    warnings.filterwarnings("ignore", category=RuntimeWarning)

    if not a.show:
        matplotlib.use("Agg")
    plt = style()

    path = trouver_log(a.log)
    D = charger(path, a.debut, a.fin)
    nom = os.path.splitext(os.path.basename(path))[0]
    out = a.out or os.path.splitext(path)[0] + "_graphs"
    os.makedirs(out, exist_ok=True)

    G = D if a.complet else compacter(D)          # données des graphiques

    pages = [("1_conduite", p_conduite), ("2_puissance", p_puissance),
             ("3_thermique", p_thermique), ("4_correlations", p_correlations),
             ("5_sante", p_sante), ("6_moteur", p_moteur)]
    for fichier, fn in pages:
        fig = fn(plt, G, nom)
        if fichier != "4_correlations":
            fig.tight_layout(rect=(0, 0, 1, 0.97))
        fig.savefig(os.path.join(out, fichier + ".png"))
        if not a.show:
            plt.close(fig)

    txt = resume(D, path)
    if G.get("coupures"):
        txt += "\nGraphiques : %.0f min de temps morts retirés (%d coupures, --complet pour tout garder)" % (
            G["retire_min"], len(G["coupures"]))
    with open(os.path.join(out, "resume.txt"), "w", encoding="utf-8") as f:
        f.write(txt + "\n")
    print(txt)
    print("\nGraphiques dans :", out)
    if a.show:
        plt.show()


if __name__ == "__main__":
    main()
