# -*- coding: utf-8 -*-
"""app.py — assemble transport et ecrans."""

import sys
import time
import argparse

from PySide6.QtCore import Qt
from PySide6.QtGui import QFont
from PySide6.QtWidgets import QApplication, QStackedWidget

from . import config as cfg
from .theme import QSS, PALETTE
from .link import make_link
from .screens import StartScreen, TelemetryScreen


class MainWindow(QStackedWidget):
    def __init__(self, link, title="Hydrofoil"):
        super().__init__()
        self.link = link
        self.last_rx = 0.0

        self.start_screen = StartScreen()
        self.telemetry_screen = TelemetryScreen()
        self.addWidget(self.start_screen)
        self.addWidget(self.telemetry_screen)

        self.start_screen.started.connect(self._on_start)
        self.start_screen.quit_requested.connect(self._quit)
        self.telemetry_screen.stopped.connect(self._on_stop)
        self.link.telemetry.connect(self._on_telemetry)

        self.setFixedSize(cfg.SCREEN_W, cfg.SCREEN_H)
        self.setWindowTitle(title)

    def _on_telemetry(self, d: dict):
        self.last_rx = time.time()
        if self.currentWidget() is self.telemetry_screen:
            self.telemetry_screen.update_data(d)

    def _quit(self):
        """Stop au Teensy, fermeture du lien, sortie de l'application."""
        self.link.send(cfg.STOP_PAYLOAD)
        self.link.stop_link()
        QApplication.quit()

    def _on_start(self, payload: dict):
        self.link.send(payload)
        self.setCurrentWidget(self.telemetry_screen)

    def _on_stop(self):
        self.link.send(cfg.STOP_PAYLOAD)
        self.setCurrentWidget(self.start_screen)

    def keyPressEvent(self, e):
        if e.key() == Qt.Key.Key_Escape:
            self._quit()


def parse_args(argv=None):
    ap = argparse.ArgumentParser(description="UI tactile hydrofoil")
    ap.add_argument("--port", default=cfg.SERIAL_PORT)
    ap.add_argument("--baud", type=int, default=cfg.BAUD)
    ap.add_argument("--sim", action="store_true", help="donnees simulees")
    ap.add_argument("--windowed", action="store_true", help="fenetre au lieu du plein ecran")
    return ap.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)

    app = QApplication(sys.argv)
    app.setStyleSheet(QSS)
    app.setFont(QFont(PALETTE["font"], 11))

    link = make_link(args.port, args.baud, cfg.ESC_IDS, args.sim)
    link.status.connect(lambda s: print("[link]", s))

    win = MainWindow(link, "Hydrofoil" + (" (sim)" if args.sim else ""))
    link.start_link()

    win.show() if args.windowed else win.showFullScreen()
    code = app.exec()
    link.stop_link()
    return code