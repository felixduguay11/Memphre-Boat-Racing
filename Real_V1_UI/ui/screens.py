# -*- coding: utf-8 -*-
"""screens.py — les ecrans. Ils lisent config.py, ils ne codent rien en dur."""

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QWidget, QLabel, QPushButton, QVBoxLayout, QHBoxLayout, QGridLayout
)

from . import config as cfg
from .widgets import Card, Badge

CENTER = Qt.AlignmentFlag.AlignCenter


def _close_button(slot) -> QPushButton:
    b = QPushButton("Close")
    b.setObjectName("ghost")
    b.setFixedSize(110, 44)
    b.clicked.connect(slot)
    return b


class StartScreen(QWidget):
    """Ecran d'accueil : titre centre, demarrage, fermeture."""
    started = Signal(dict)
    quit_requested = Signal()

    def __init__(self, payload: dict = None):
        super().__init__()
        self._payload = payload or cfg.START_PAYLOAD

        root = QVBoxLayout(self)
        root.setContentsMargins(22, 18, 22, 18)
        root.setSpacing(12)

        bar = QHBoxLayout()
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
        for n, i in enumerate(esc_ids):
            c = Card("ESC %d" % i)
            grid.addWidget(c, n // 2, n % 2)
            self._esc_cards[i] = c
        root.addLayout(grid)

        root.addStretch()

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
            seen.add(e.get("id"))
            if e.get("ok", 1):
                card.set_value(cfg.esc_value(e))
                card.set_sub(cfg.esc_sub(e))
            else:
                card.set_value("--")
                card.set_sub(cfg.esc_offline_sub(e))
        for i, card in self._esc_cards.items():
            if i not in seen:
                card.set_value("--")
                card.set_sub("absent de la trame")