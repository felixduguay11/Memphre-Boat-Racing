# -*- coding: utf-8 -*-
"""screens.py — les ecrans. Ils lisent config.py, ils ne codent rien en dur."""

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QWidget, QFrame, QLabel, QPushButton, QVBoxLayout, QHBoxLayout, QGridLayout,
    QPlainTextEdit, QSizePolicy
)

from . import config as cfg
from .widgets import Card, RowCard, SpeedCard, Badge, EscCard, set_status_style

CENTER = Qt.AlignmentFlag.AlignCenter


def _close_button(slot) -> QPushButton:
    b = QPushButton("Close")
    b.setObjectName("ghost")
    b.setFixedSize(130, 52)
    b.clicked.connect(slot)
    return b


# Pages de telemetrie, dans l'ordre des onglets
PAGES = [("moteurs", "Moteurs"), ("xsens", "Xsens")]


class _PageBar(QHBoxLayout):
    """Barre commune aux pages de telemetrie : onglets a gauche,
    consigne et etat (mode · run) a droite."""

    def __init__(self, active: str, on_page):
        super().__init__()
        self.setSpacing(8)
        for key, text in PAGES:
            b = QPushButton(text)
            b.setObjectName("tabOn" if key == active else "tab")
            b.setFixedSize(122, 50)
            b.clicked.connect(lambda _=False, k=key: on_page(k))
            self.addWidget(b)
        self.target = QLabel("--")
        self.target.setObjectName("barHint")
        # Si la place manque, c'est la consigne qui est rognee, jamais l'etat
        self.target.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        self.target.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        self.mode = Badge("--", True)
        self.addWidget(self.target, 1)
        self.addSpacing(10)
        self.addWidget(self.mode)

    def update_data(self, d: dict):
        self.mode.set_state(cfg.mode_text(d), cfg.mode_ok(d))
        self.target.setText(cfg.target_text(d))


def _stop_button(slot) -> QPushButton:
    stop = QPushButton("Stop")
    stop.setObjectName("danger")
    stop.clicked.connect(slot)
    return stop


class StartScreen(QWidget):
    """Ecran d'accueil : titre centre, demarrage, theme, fermeture."""
    started = Signal(dict)
    quit_requested = Signal()
    theme_toggled = Signal()
    flash_requested = Signal()

    def __init__(self, payload: dict = None):
        super().__init__()
        self._payload = payload or cfg.START_PAYLOAD

        root = QVBoxLayout(self)
        root.setContentsMargins(22, 18, 22, 18)
        root.setSpacing(12)

        bar = QHBoxLayout()
        self.theme_btn = QPushButton()
        self.theme_btn.setObjectName("ghost")
        self.theme_btn.setFixedSize(200, 52)
        self.theme_btn.clicked.connect(self.theme_toggled.emit)
        bar.addWidget(self.theme_btn)
        flash = QPushButton("Mise a jour Teensy")
        flash.setObjectName("ghost")
        flash.setFixedSize(250, 52)
        flash.clicked.connect(self.flash_requested.emit)
        bar.addWidget(flash)
        bar.addStretch()
        bar.addWidget(_close_button(self.quit_requested.emit))
        root.addLayout(bar)

        root.addStretch(1)

        titre = QLabel("Memphre Boat Racing")
        titre.setObjectName("title")
        titre.setAlignment(CENTER)
        root.addWidget(titre)

        root.addStretch(2)

        go = QPushButton("Demarrer")
        go.setObjectName("primary")
        go.clicked.connect(lambda: self.started.emit(dict(self._payload)))
        root.addWidget(go)

    def set_theme_label(self, current: str):
        """Le bouton annonce le theme vers lequel il bascule."""
        self.theme_btn.setText("Theme clair" if current == "sombre" else "Theme sombre")


class FlashScreen(QWidget):
    """Mise a jour du Teensy : avertissement, lancement, sortie de pio.
    Le bouton Lancer est une confirmation volontaire : le Teensy
    redemarre pendant l'operation, les moteurs s'arretent."""
    run_requested = Signal()
    back_requested = Signal()

    def __init__(self):
        super().__init__()
        root = QVBoxLayout(self)
        root.setContentsMargins(22, 18, 22, 18)
        root.setSpacing(10)

        bar = QHBoxLayout()
        titre = QLabel("Mise a jour Teensy")
        titre.setObjectName("title")
        bar.addWidget(titre)
        bar.addStretch()
        self._back = QPushButton("Retour")
        self._back.setObjectName("ghost")
        self._back.setFixedSize(130, 52)
        self._back.clicked.connect(self.back_requested.emit)
        bar.addWidget(self._back)
        root.addLayout(bar)

        hint = QLabel("Bateau a quai, helices hors de l'eau. Le Teensy redemarre : "
                      "les moteurs s'arretent pendant la mise a jour.")
        hint.setObjectName("hint")
        hint.setWordWrap(True)
        root.addWidget(hint)

        self._log = QPlainTextEdit()
        self._log.setObjectName("flashLog")
        self._log.setReadOnly(True)
        self._log.setMaximumBlockCount(500)
        root.addWidget(self._log, 1)

        self._state = QLabel("")
        self._state.setObjectName("flashBusy")
        self._state.setWordWrap(True)
        root.addWidget(self._state)

        self._go = QPushButton("Lancer la mise a jour")
        self._go.setObjectName("primary")
        self._go.clicked.connect(self.run_requested.emit)
        root.addWidget(self._go)

    def reset(self):
        self._log.clear()
        self._set_state("", "flashBusy")
        self.set_busy(False)

    def set_busy(self, busy: bool):
        self._go.setEnabled(not busy)
        self._back.setEnabled(not busy)

    def append(self, line: str):
        self._log.appendPlainText(line)

    def show_state(self, text: str):
        self._set_state(text, "flashBusy")

    def show_result(self, ok: bool, text: str):
        self._set_state(text, "flashOk" if ok else "flashKo")

    def _set_state(self, text, name):
        self._state.setText(text)
        self._state.setObjectName(name)
        self._state.style().unpolish(self._state)
        self._state.style().polish(self._state)


class TelemetryScreen(QWidget):
    """Page Moteurs : batterie / courant / puissance et une carte par ESC.
    Cartes generees depuis cfg."""
    stopped = Signal()
    page_requested = Signal(str)

    def __init__(self, metrics=None, esc_ids=None, columns: int = None):
        super().__init__()
        metrics = metrics or cfg.TOP_METRICS
        esc_ids = esc_ids or cfg.ESC_IDS
        columns = columns or cfg.TOP_COLUMNS

        root = QVBoxLayout(self)
        root.setContentsMargins(16, 12, 16, 12)
        root.setSpacing(8)

        self._bar = _PageBar("moteurs", self.page_requested.emit)
        root.addLayout(self._bar)

        top = QGridLayout()
        top.setSpacing(8)
        self._metrics = []
        for n, m in enumerate(metrics):
            c = Card(m.label, m.unit)
            top.addWidget(c, n // columns, n % columns)
            self._metrics.append((m, c))
        for k in range(columns):
            top.setColumnStretch(k, 1)              # colonnes de largeur egale
        root.addLayout(top)

        # Rangee du milieu : ESC | vitesse GPS | ESC (ordre de cfg.ESC_IDS)
        grid = QGridLayout()
        grid.setSpacing(8)
        self._esc_cards = {}
        self._temp_ok = {}      # (id ESC, cle) -> etat precedent, pour l'hysteresis
        cols = [0, 2]           # colonne 1 = vitesse
        for n, i in enumerate(esc_ids):
            c = EscCard("ESC %d" % i, cfg.esc_unit(), len(cfg.ESC_TEMPS))
            grid.addWidget(c, n // 2, cols[n % 2])
            self._esc_cards[i] = c
        self._speed = SpeedCard(cfg.SPEED_LABEL_COURT, cfg.SPEED_UNIT)
        self._speed.setFixedWidth(220)
        grid.addWidget(self._speed, 0, 1, max(1, (len(esc_ids) + 1) // 2), 1)
        grid.setColumnStretch(0, 1)
        grid.setColumnStretch(2, 1)
        root.addLayout(grid, 1)     # les cartes ESC prennent la hauteur libre

        root.addWidget(_stop_button(self.stopped.emit))

    def update_data(self, d: dict):
        self._bar.update_data(d)
        self._speed.set_value(cfg.speed_text(d))

        for m, card in self._metrics:
            try:
                card.set_value(m.value(d))
            except Exception:
                card.set_value("--")

        seen = set()
        for e in d.get("esc", []) or []:
            card = self._esc_cards.get(e.get("id"))
            if card is None:
                continue
            i = e.get("id")
            seen.add(i)
            if e.get("ok", 1):
                card.set_value(cfg.esc_value(e))
                card.set_unit(cfg.esc_unit())
                self._update_temps(i, card, e)
            else:
                self._esc_offline(i, card, cfg.esc_offline_sub(e))
        for i, card in self._esc_cards.items():
            if i not in seen:
                self._esc_offline(i, card, "absent de la trame")

    def _update_temps(self, esc_id, card, e: dict):
        for n, chk in enumerate(cfg.ESC_TEMPS):
            t = e.get(chk.key)
            if t is None:
                card.set_status(n, "%s : --" % chk.label, None)
                continue
            key = (esc_id, chk.key)
            ok = cfg.temp_ok(t, chk.limit, self._temp_ok.get(key, True))
            self._temp_ok[key] = ok
            card.set_status(n, cfg.temp_text(chk.label, t, ok), ok)

    def _esc_offline(self, esc_id, card, reason: str):
        card.set_value("--")
        card.set_unit("")
        card.clear_status()
        card.set_status(0, reason, None)    # raison sous le chiffre (place)
        for chk in cfg.ESC_TEMPS:
            self._temp_ok.pop((esc_id, chk.key), None)


class XsensScreen(QWidget):
    """Page Xsens : vitesse GPS en tres gros, angles, etat IMU / GPS."""
    stopped = Signal()
    page_requested = Signal(str)

    def __init__(self, metrics=None):
        super().__init__()
        metrics = metrics or cfg.IMU_METRICS

        root = QVBoxLayout(self)
        root.setContentsMargins(16, 12, 16, 12)
        root.setSpacing(8)

        self._bar = _PageBar("xsens", self.page_requested.emit)
        root.addLayout(self._bar)

        # Vitesse (gauche, large) + angles (droite, une ligne chacun)
        main = QHBoxLayout()
        main.setSpacing(8)
        self._speed = Card(cfg.SPEED_LABEL, cfg.SPEED_UNIT, "speedValue",
                           center=True, unit_name="speedUnit")
        main.addWidget(self._speed, 1)

        # Colonne des angles a largeur fixe : la mise en page ne bouge
        # pas quand les valeurs changent de longueur.
        colw = QWidget()
        colw.setFixedWidth(280)
        col = QVBoxLayout(colw)
        col.setContentsMargins(0, 0, 0, 0)
        col.setSpacing(8)
        self._metrics = []
        for m in metrics:
            c = RowCard(m.label, m.unit)
            col.addWidget(c, 1)
            self._metrics.append((m, c))
        main.addWidget(colw)
        root.addLayout(main, 1)

        # Ligne du bas : etats IMU / GPS, position, altitude
        info = QFrame()
        info.setObjectName("card")
        lay = QHBoxLayout(info)
        lay.setContentsMargins(12, 6, 12, 6)
        lay.setSpacing(12)
        self._imu_state = QLabel("--")
        self._gps_state = QLabel("--")
        for w in (self._imu_state, self._gps_state):
            set_status_style(w, None)
            lay.addWidget(w)
        lay.addStretch()
        self._pos = QLabel("--");  self._pos.setObjectName("smallValue")
        self._alt = QLabel("--");  self._alt.setObjectName("smallValue")
        lay.addWidget(self._pos)
        lay.addSpacing(10)
        lay.addWidget(self._alt)
        root.addWidget(info)

        root.addWidget(_stop_button(self.stopped.emit))

    def update_data(self, d: dict):
        self._bar.update_data(d)
        self._speed.set_value(cfg.speed_text(d))
        for m, card in self._metrics:
            try:
                card.set_value(m.value(d))
            except Exception:
                card.set_value("--")
        for w, (text, ok) in ((self._imu_state, cfg.imu_status(d)),
                              (self._gps_state, cfg.gps_status(d))):
            w.setText(text)
            set_status_style(w, ok)
        self._pos.setText(cfg.position_text(d))
        self._alt.setText(cfg.altitude_text(d))

