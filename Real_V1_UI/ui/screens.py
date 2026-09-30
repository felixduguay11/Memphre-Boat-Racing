# -*- coding: utf-8 -*-
"""screens.py — les ecrans. Ils lisent config.py, ils ne codent rien en dur."""

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QWidget, QLabel, QPushButton, QVBoxLayout, QHBoxLayout, QGridLayout,
    QPlainTextEdit
)

from . import config as cfg
from .widgets import Card, Badge, EscCard

CENTER = Qt.AlignmentFlag.AlignCenter


def _close_button(slot) -> QPushButton:
    b = QPushButton("Close")
    b.setObjectName("ghost")
    b.setFixedSize(110, 44)
    b.clicked.connect(slot)
    return b


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
        self.theme_btn.setFixedSize(170, 44)
        self.theme_btn.clicked.connect(self.theme_toggled.emit)
        bar.addWidget(self.theme_btn)
        flash = QPushButton("Mise a jour Teensy")
        flash.setObjectName("ghost")
        flash.setFixedSize(210, 44)
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
        self._back.setFixedSize(110, 44)
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
    """Affiche une trame de telemetrie ; cartes generees depuis cfg."""
    stopped = Signal()

    def __init__(self, metrics=None, esc_ids=None, columns: int = None):
        super().__init__()
        metrics = metrics or cfg.TOP_METRICS
        esc_ids = esc_ids or cfg.ESC_IDS
        columns = columns or cfg.TOP_COLUMNS

        root = QVBoxLayout(self)
        root.setContentsMargins(22, 18, 22, 18)
        root.setSpacing(10)

        bar = QHBoxLayout()
        titre = QLabel("Telemetrie")
        titre.setObjectName("title")
        self.target = QLabel("--")
        self.target.setObjectName("hint")
        self.mode = Badge("--", True)
        bar.addWidget(titre)
        bar.addStretch()
        bar.addWidget(self.target)
        bar.addSpacing(14)
        bar.addWidget(self.mode)
        root.addLayout(bar)

        top = QGridLayout()
        top.setSpacing(10)
        self._metrics = []
        for n, m in enumerate(metrics):
            c = Card(m.label, m.unit)
            top.addWidget(c, n // columns, n % columns)
            self._metrics.append((m, c))
        root.addLayout(top)

        grid = QGridLayout()
        grid.setSpacing(10)
        self._esc_cards = {}
        self._temp_ok = {}      # (id ESC, cle) -> etat precedent, pour l'hysteresis
        for n, i in enumerate(esc_ids):
            c = EscCard("ESC %d" % i, cfg.esc_unit(), len(cfg.ESC_TEMPS))
            grid.addWidget(c, n // 2, n % 2)
            self._esc_cards[i] = c
        root.addLayout(grid, 1)     # les cartes ESC prennent la hauteur libre

        stop = QPushButton("Stop")
        stop.setObjectName("danger")
        stop.clicked.connect(self.stopped.emit)
        root.addWidget(stop)

    def update_data(self, d: dict):
        self.mode.set_state(cfg.mode_text(d), True)
        self.target.setText(cfg.target_text(d))

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
        card.set_unit(reason)
        card.clear_status()
        for chk in cfg.ESC_TEMPS:
            self._temp_ok.pop((esc_id, chk.key), None)