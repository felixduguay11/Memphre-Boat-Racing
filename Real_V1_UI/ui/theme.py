# -*- coding: utf-8 -*-
"""theme.py — deux palettes (sombre / clair), le QSS est derive de celle
choisie. Le choix est memorise dans THEME_FILE et survit au redemarrage."""

import os

FONT = "DejaVu Sans"

PALETTES = {
    "sombre": {
        "bg":        "#12161c",
        "card":      "#1b212a",
        "line":      "#2c3542",
        "text":      "#e8edf4",
        "muted":     "#93a1b3",
        "accent":    "#4a9eff",
        "accent_bg": "#15283d",
        "accent_fg": "#8cc4ff",
        "ok_bg":     "#16362a",
        "ok_fg":     "#57d9a3",
        "ko_bg":     "#3a1c1c",
        "ko_fg":     "#f08a8a",
        "danger":    "#bb4444",
        "danger_bg": "#331a1a",
        "danger_fg": "#ff9b9b",
        "press":     "#0e1218",
    },
    # Clair : pense pour le plein soleil, contraste plus fort que le sombre.
    "clair": {
        "bg":        "#eef1f5",
        "card":      "#ffffff",
        "line":      "#b9c3cf",
        "text":      "#0f161e",
        "muted":     "#4f5b6a",
        "accent":    "#1f6fd1",
        "accent_bg": "#dce9fb",
        "accent_fg": "#12508f",
        "ok_bg":     "#d2f0e1",
        "ok_fg":     "#0d6b43",
        "ko_bg":     "#fbdada",
        "ko_fg":     "#a51d1d",
        "danger":    "#c0392b",
        "danger_bg": "#fbe3e3",
        "danger_fg": "#a01c1c",
        "press":     "#d3dae3",
    },
}

DEFAULT_THEME = "sombre"
THEME_FILE = os.path.expanduser("~/.memphre_theme")

_TEMPLATE = """
QWidget        {{ background:{bg}; color:{text}; font-family:"{font}"; }}
QLabel#title   {{ font-size:32px; font-weight:600; }}
QLabel#hint    {{ font-size:20px; color:{muted}; }}
QLabel#barHint {{ font-size:18px; color:{muted}; }}
QLabel#pill    {{ font-size:22px; font-weight:600; padding:6px 14px; border-radius:16px;
                  background:{ok_bg}; color:{ok_fg}; }}
QLabel#pillKo  {{ font-size:22px; font-weight:600; padding:6px 14px; border-radius:16px;
                  background:{ko_bg}; color:{ko_fg}; }}
QFrame#card    {{ background:{card}; border-radius:10px; }}
QLabel#cardLabel {{ font-size:20px; color:{muted}; }}
QLabel#cardValue {{ font-size:56px; font-weight:600; }}
QLabel#cardUnit  {{ font-size:24px; color:{muted}; }}
QLabel#cardSub   {{ font-size:18px; color:{muted}; }}
QFrame#card QLabel {{ background:transparent; }}
QLabel#escValue  {{ font-size:88px; font-weight:600; }}
QLabel#speedValue {{ font-size:150px; font-weight:600; }}
QLabel#speedUnit  {{ font-size:32px; color:{muted}; }}
QLabel#rowValue   {{ font-size:50px; font-weight:600; }}
QLabel#smallValue {{ font-size:24px; font-weight:500; }}
QLabel#statusOk   {{ font-size:22px; color:{ok_fg}; padding:2px 10px; }}
QLabel#statusNone {{ font-size:22px; color:{muted}; padding:2px 10px; }}
QFrame#card QLabel#statusKo {{ font-size:22px; font-weight:600; color:{ko_fg};
                     background:{ko_bg}; border-radius:8px; padding:2px 10px; }}
QPushButton#primary {{ font-size:28px; border-radius:10px; border:2px solid {accent};
                       background:{accent_bg}; color:{accent_fg}; min-height:66px; }}
QPushButton#danger  {{ font-size:26px; border-radius:10px; border:2px solid {danger};
                       background:{danger_bg}; color:{danger_fg}; min-height:54px; }}
QPushButton#ghost {{ font-size:20px; border-radius:10px; border:1px solid {line};
                     background:transparent; color:{muted}; }}
QPushButton#tab   {{ font-size:22px; border-radius:10px; border:1px solid {line};
                     background:transparent; color:{muted}; }}
QPushButton#tabOn {{ font-size:22px; font-weight:600; border-radius:10px; border:2px solid {accent};
                     background:{accent_bg}; color:{accent_fg}; }}
QPushButton:pressed {{ background:{press}; }}
QPushButton:disabled {{ color:{line}; border-color:{line}; background:transparent; }}
QPlainTextEdit#flashLog {{ background:{card}; color:{text}; border:none; border-radius:10px;
                           padding:6px; font-family:"DejaVu Sans Mono"; font-size:14px; }}
QLabel#flashOk   {{ font-size:20px; font-weight:600; color:{ok_fg}; }}
QLabel#flashKo   {{ font-size:20px; font-weight:600; color:{ko_fg}; }}
QLabel#flashBusy {{ font-size:20px; color:{accent_fg}; }}
"""


def build_qss(name: str) -> str:
    return _TEMPLATE.format(font=FONT, **PALETTES[name])


def other_theme(name: str) -> str:
    return "clair" if name == "sombre" else "sombre"


def load_theme() -> str:
    try:
        with open(THEME_FILE, encoding="utf-8") as f:
            name = f.read().strip()
        return name if name in PALETTES else DEFAULT_THEME
    except OSError:
        return DEFAULT_THEME


def save_theme(name: str) -> None:
    # Un echec d'ecriture ne doit jamais empecher de changer le theme.
    try:
        with open(THEME_FILE, "w", encoding="utf-8") as f:
            f.write(name)
    except OSError as e:
        print("[theme] sauvegarde impossible:", e)
