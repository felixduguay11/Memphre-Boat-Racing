# -*- coding: utf-8 -*-
"""app.py — assemble transport et ecrans."""

import sys
import argparse

from PySide6.QtCore import Qt
from PySide6.QtGui import QFont
from PySide6.QtWidgets import QApplication, QStackedWidget

from . import config as cfg
from . import theme
from .link import make_link
from .screens import StartScreen, TelemetryScreen, FlashScreen
from .flasher import TeensyFlasher
from .logger import TelemetryLogger


class MainWindow(QStackedWidget):
    def __init__(self, link, title="Hydrofoil"):
        super().__init__()
        self.link = link

        self.start_screen = StartScreen()
        self.telemetry_screen = TelemetryScreen()
        self.flash_screen = FlashScreen()
        self.addWidget(self.start_screen)
        self.addWidget(self.telemetry_screen)
        self.addWidget(self.flash_screen)

        self._link_released = False
        self.flasher = TeensyFlasher(cfg.FW_DIR, cfg.PIO_BIN, self._release_link,
                                     cfg.GIT_PULL)
        self.flasher.output.connect(self.flash_screen.append)
        self.flasher.state.connect(self.flash_screen.show_state)
        self.flasher.finished.connect(self._on_flash_done)

        self.start_screen.started.connect(self._on_start)
        self.start_screen.quit_requested.connect(self._quit)
        self.start_screen.theme_toggled.connect(self._toggle_theme)
        self.start_screen.flash_requested.connect(self._open_flash)
        self.flash_screen.run_requested.connect(self._start_flash)
        self.flash_screen.back_requested.connect(
            lambda: self.setCurrentWidget(self.start_screen))
        self.telemetry_screen.stopped.connect(self._on_stop)
        self.link.telemetry.connect(self._on_telemetry)

        self.setFixedSize(cfg.SCREEN_W, cfg.SCREEN_H)
        self.setWindowTitle(title)
        
        self.logger = TelemetryLogger(cfg.LOG_DIR)
        self.apply_theme(theme.load_theme())

    def apply_theme(self, name: str):
        self.theme_name = name
        QApplication.instance().setStyleSheet(theme.build_qss(name))
        self.start_screen.set_theme_label(name)

    def _toggle_theme(self):
        name = theme.other_theme(self.theme_name)
        self.apply_theme(name)
        theme.save_theme(name)

    def _on_telemetry(self, d: dict):
        if self.currentWidget() is self.telemetry_screen:
            self.telemetry_screen.update_data(d)
            self.logger.write(d)

    def _quit(self):
        """Stop au Teensy, fermeture du log, sortie de l'application.
        Le lien est ferme par main() apres la boucle Qt."""
        self.logger.stop()
        self.link.send(cfg.STOP_PAYLOAD)
        QApplication.quit()

    def _on_start(self, payload: dict):
        self.link.send(payload)
        self.setCurrentWidget(self.telemetry_screen)
        self.logger.start()

    def _on_stop(self):
        self.link.send(cfg.STOP_PAYLOAD)
        self.logger.stop()
        self.setCurrentWidget(self.start_screen)

    # ------------------------------------------------------------ mise a jour Teensy
    def _open_flash(self):
        self.flash_screen.reset()
        self.setCurrentWidget(self.flash_screen)

    def _start_flash(self):
        self.flash_screen.set_busy(True)
        self.flasher.start()

    def _release_link(self):
        """Appele par le flasher juste avant le televersement."""
        self.link.stop_link()
        self._link_released = True

    def _on_flash_done(self, ok: bool, msg: str):
        if self._link_released:
            # Le Teensy redemarre ; le lien reessaie d'ouvrir le port
            # chaque seconde jusqu'a ce qu'il reapparaisse.
            self.link.start_link()
            self._link_released = False
        self.flash_screen.show_result(ok, msg)
        self.flash_screen.set_busy(False)

    def keyPressEvent(self, e):
        # Pas de sortie pendant une mise a jour : un televersement
        # interrompu oblige a reflasher avec le bouton du Teensy.
        if e.key() == Qt.Key.Key_Escape and not self.flasher.busy:
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
    app.setFont(QFont(theme.FONT, 11))

    link = make_link(args.port, args.baud, cfg.ESC_IDS, args.sim)
    link.status.connect(lambda s: print("[link]", s))

    win = MainWindow(link, "Hydrofoil" + (" (sim)" if args.sim else ""))
    link.start_link()

    win.show() if args.windowed else win.showFullScreen()
    code = app.exec()
    link.stop_link()
    return code
