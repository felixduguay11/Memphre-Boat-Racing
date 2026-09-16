# -*- coding: utf-8 -*-
"""theme.py — une seule palette, le QSS en est derive."""

PALETTE = {
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
    "press":     "#0e1218",
    "font":      "DejaVu Sans",
}

QSS = """
QWidget        {{ background:{bg}; color:{text}; font-family:"{font}"; }}
QLabel#title   {{ font-size:26px; font-weight:500; }}
QLabel#hint    {{ font-size:15px; color:{muted}; }}
QLabel#pill    {{ font-size:14px; padding:5px 14px; border-radius:14px;
                  background:{ok_bg}; color:{ok_fg}; }}
QLabel#pillKo  {{ font-size:14px; padding:5px 14px; border-radius:14px;
                  background:{ko_bg}; color:{ko_fg}; }}
QFrame#card    {{ background:{card}; border-radius:10px; }}
QLabel#cardLabel {{ font-size:14px; color:{muted}; }}
QLabel#cardValue {{ font-size:30px; font-weight:500; }}
QLabel#cardSub   {{ font-size:14px; color:{muted}; }}
QPushButton#preset {{
    text-align:left; padding:12px 18px; border-radius:10px;
    border:1px solid {line}; background:{card}; font-size:19px;
}}
QPushButton#preset:checked {{ border:2px solid {accent}; background:{accent_bg}; }}
QPushButton#primary {{ font-size:22px; border-radius:10px; border:2px solid {accent};
                       background:{accent_bg}; color:{accent_fg}; min-height:66px; }}
QPushButton#danger  {{ font-size:22px; border-radius:10px; border:2px solid #b44;
                       background:#331a1a; color:#ff9b9b; min-height:66px; }}
QPushButton#ghost {{ font-size:17px; border-radius:10px; border:1px solid {line};
                     background:transparent; color:{muted}; }}
QPushButton:pressed {{ background:{press}; }}
""".format(**PALETTE)