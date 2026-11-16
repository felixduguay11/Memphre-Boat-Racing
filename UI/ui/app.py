# -*- coding: utf-8 -*-
"""app.py — assemble transport et ecrans."""

import sys
import time
import argparse

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QFont
from PySide6.QtWidgets import QApplication, QStackedWidget

from . import config as cfg
from . import theme
from .link import make_link
from .screens import (StartScreen, TelemetryScreen, XsensScreen, BmsScreen,
                      FlashScreen, CalibScreen)
from .bms import make_bms
from .flasher import TeensyFlasher
from .logger import TelemetryLogger
from .esc_params import EscParamsChecker
from .esc_popup import EscParamsPopup


class MainWindow(QStackedWidget):
    def __init__(self, link, title="Hydrofoil", bms=None):
        super().__init__()
        self.link = link
        self.bms = bms

        self.start_screen = StartScreen()
        self.telemetry_screen = TelemetryScreen()
        self.xsens_screen = XsensScreen()
        self.bms_screen = BmsScreen()
        self.flash_screen = FlashScreen()
        self.calib_screen = CalibScreen()
        self.addWidget(self.start_screen)
        self.addWidget(self.telemetry_screen)
        self.addWidget(self.xsens_screen)
        self.addWidget(self.bms_screen)
        self.addWidget(self.flash_screen)
        self.addWidget(self.calib_screen)

        # Pages de telemetrie (onglets), toutes alimentees par la trame
        self._pages = {"moteurs": self.telemetry_screen, "xsens": self.xsens_screen,
                       "bms": self.bms_screen}
        self._streaming = False

        # BMS : derniere mesure, ajoutee a chaque trame du Teensy sous "bms"
        self._bms = None            # dernier echantillon recu
        self._bms_t = 0.0           # heure de reception (monotonic)
        self._bms_low_n = 0         # mesures consecutives sous BMS_CAP_SOC
        self._bat_cap = False       # plafond batterie demande au Teensy
        self._packs = {}            # plusieurs BMS : n -> (mesure, heure)
        self._last_frame = {}
        if self.bms is not None:
            self.bms.sample.connect(self._on_bms)
        # Rafraichit la page BMS meme si le Teensy n'envoie rien
        # (et fait passer a "--" une mesure perimee).
        self._bms_timer = QTimer(self)
        self._bms_timer.setInterval(1000)
        self._bms_timer.timeout.connect(self._refresh_bms_page)
        self._bms_timer.start()

        self._link_released = False
        self.flasher = TeensyFlasher(
            cfg.FW_DIR, cfg.PIO_BIN, self._release_link, cfg.GIT_PULL,
            hex_path=cfg.FW_HEX if cfg.FLASH_MODE == "hex" else "",
            serial_port=getattr(link, "_port", cfg.SERIAL_PORT),
            teensy_cli=cfg.TEENSY_CLI)
        self.flasher.output.connect(self.flash_screen.append)
        self.flasher.state.connect(self.flash_screen.show_state)
        self.flasher.finished.connect(self._on_flash_done)

        self.start_screen.started.connect(self._on_start)
        self.start_screen.quit_requested.connect(self._quit)
        self.start_screen.theme_toggled.connect(self._toggle_theme)
        self.start_screen.flash_requested.connect(self._open_flash)
        self.start_screen.calib_requested.connect(self._open_calib)
        self.calib_screen.start_requested.connect(
            lambda: self.link.send(cfg.CALIB_START_PAYLOAD))
        self.calib_screen.finish_requested.connect(
            lambda: self.link.send(cfg.CALIB_FIN_PAYLOAD))
        self.calib_screen.cancel_requested.connect(
            lambda: self.link.send(cfg.CALIB_ANNULE_PAYLOAD))
        self.calib_screen.back_requested.connect(self._close_calib)
        self.flash_screen.run_requested.connect(self._start_flash)
        self.flash_screen.back_requested.connect(
            lambda: self.setCurrentWidget(self.start_screen))
        for page in self._pages.values():
            page.stopped.connect(self._on_stop)
            page.page_requested.connect(self._show_page)
        self.link.telemetry.connect(self._on_telemetry)

        self.setFixedSize(cfg.SCREEN_W, cfg.SCREEN_H)
        self.setWindowTitle(title)
        
        self.logger = TelemetryLogger(cfg.LOG_DIR)

        # Parametres des VESC : verification a chaque lecture du Teensy
        self.esc_checker = EscParamsChecker(cfg.ESC_PARAMS_DIR, cfg.LOG_DIR)
        self.esc_popup = EscParamsPopup(self)
        self.currentChanged.connect(self._keep_popup_on_top)
        # Si le Teensy a lu les configs avant le demarrage de l'UI : on les
        # redemande (le port serie s'ouvre dans les premieres secondes).
        for ms in (3000, 8000):
            QTimer.singleShot(ms, self._ask_escconf)

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
        if d.get("type") == "escconf":
            self._on_escconf(d)
            return
        self._last_frame = d
        view = self._with_bms(d)
        # Toutes les pages sont a jour : changer d'onglet n'affiche
        # jamais une valeur perimee. Le log tourne quel que soit l'onglet.
        if self._streaming:
            for page in self._pages.values():
                page.update_data(view)
            self.logger.write(d)            # le BMS a ses propres lignes
        elif self.currentWidget() is self.calib_screen:
            self.calib_screen.update_data(d)

    # ------------------------------------------------------------ BMS
    def _on_bms(self, sample: dict):
        now = time.monotonic()
        k = sample.pop("_pack", None)
        if k is None:                       # une seule batterie
            self._bms, self._bms_t = sample, now
            # Log : une ligne par mesure BMS (1 Hz), a part des trames Teensy
            self.logger.write({"type": "bms", "bms": cfg.bms_log_record(sample)})
        else:                               # plusieurs batteries
            self._packs[k] = (sample, now)
            self.logger.write({"type": "bms", "bms%d" % (k + 1): cfg.bms_log_record(sample)})
            n = getattr(self.bms, "n", len(self._packs))
            if len(self._packs) < n:
                return                      # on attend une mesure de chaque pack
            packs = [self._packs[i] for i in sorted(self._packs)]
            self._bms = cfg.bms_combine([p for p, _ in packs])
            # Age = celui du pack le plus vieux : un BMS muet rend tout perime
            self._bms_t = min(t for _, t in packs)
            if now - self._bms_t >= cfg.BMS_STALE_S:
                return                      # pas de plafond sur une donnee perimee
        self._check_bms_cap(self._bms.get("battery_level"))

    def _check_bms_cap(self, soc):
        """Plafond de consigne quand la charge tombe a BMS_CAP_SOC.
        Pas d'arret : le pilote doit pouvoir rentrer au quai."""
        if soc is None:
            return
        if soc > cfg.BMS_CAP_SOC:
            self._bms_low_n = 0
            if self._bat_cap and soc >= cfg.BMS_CAP_SOC + cfg.BMS_CAP_REARM:
                self._bat_cap = False
                self.link.send(cfg.BATT_OK_PAYLOAD)
                self.logger.write({"type": "evt", "evt": "bat_cap_off", "soc": soc})
                self.start_screen.set_message("")
                print("[bms] charge %d %% : plafond leve" % soc)
            return
        self._bms_low_n += 1
        if self._bms_low_n < cfg.BMS_CAP_CONFIRM:
            return
        # Renvoye a chaque mesure : le Teensy l'oublie s'il redemarre
        self.link.send(cfg.BATT_BASSE_PAYLOAD)
        if not self._bat_cap:
            self._bat_cap = True
            msg = "Batterie a %d %% : vitesse limitee a %d %%" % (soc, cfg.BATT_BASSE_PCT)
            print("[bms]", msg)
            self.logger.write({"type": "evt", "evt": "bat_cap_on", "soc": soc})
            self.start_screen.set_message(msg)

    def _bms_view(self):
        if self._bms is None:
            return None
        age = time.monotonic() - self._bms_t
        v = dict(self._bms)
        v["ok"] = int(age < cfg.BMS_STALE_S)
        v["age"] = round(age, 1)
        return v

    def _with_bms(self, d: dict) -> dict:
        b = self._bms_view()
        if b is None:
            return d
        out = dict(d)
        out["bms"] = b
        return out

    def _refresh_bms_page(self):
        page = self.currentWidget()
        if page in (self.bms_screen, self.telemetry_screen):
            page.update_bms(self._with_bms(self._last_frame))

    # ------------------------------------------------------------ parametres ESC
    def _keep_popup_on_top(self, _index):
        if self.esc_popup.isVisible():
            self.esc_popup.raise_()

    def _ask_escconf(self):
        self.link.send(cfg.ESCCONF_PAYLOAD)

    def _on_escconf(self, d: dict):
        res = self.esc_checker.traiter(d)
        if res is None:
            return                          # simple renvoi d'une lecture deja vue
        if res["erreur"]:
            print("[esc_params] ESC %d %s : %s" % (res["id"], res["nom"], res["erreur"]))
        else:
            print("[esc_params] ESC %d %s : %d parametre(s) different(s)"
                  % (res["id"], res["nom"], len(res["diffs"])))
        if res["erreur"] or res["diffs"]:
            self.esc_popup.ajouter(res)

    def _show_page(self, key: str):
        self.setCurrentWidget(self._pages[key])

    def _quit(self):
        """Stop au Teensy, fermeture du log, sortie de l'application.
        Le lien est ferme par main() apres la boucle Qt."""
        self.logger.stop()
        self.link.send(cfg.STOP_PAYLOAD)
        QApplication.quit()

    def _on_start(self, payload: dict):
        self.link.send(payload)
        self._ask_escconf()
        self._streaming = True
        self.setCurrentWidget(self.telemetry_screen)
        self.logger.start()

    def _on_stop(self):
        self.link.send(cfg.STOP_PAYLOAD)
        self._streaming = False
        self.logger.stop()
        self.setCurrentWidget(self.start_screen)

    # ------------------------------------------------------------ calibration levier
    def _open_calib(self):
        # La telemetrie est necessaire pour voir le levier : on demande
        # le flux au Teensy, sans journaliser.
        self.calib_screen.reset()
        self.setCurrentWidget(self.calib_screen)
        self.link.send(cfg.START_PAYLOAD)

    def _close_calib(self):
        self.link.send(cfg.CALIB_ANNULE_PAYLOAD)   # sans effet si rien en cours
        self.link.send(cfg.STOP_PAYLOAD)
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
    ap.add_argument("--bms-sim", action="store_true", help="BMS simule (sans Bluetooth)")
    ap.add_argument("--no-bms", action="store_true", help="ne pas se connecter au BMS")
    return ap.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)

    app = QApplication(sys.argv)
    app.setFont(QFont(theme.FONT, 11))

    link = make_link(args.port, args.baud, cfg.ESC_IDS, args.sim)
    link.status.connect(lambda s: print("[link]", s))

    bms = None
    if cfg.BMS_ENABLED and not args.no_bms:
        bms = make_bms(cfg, args.bms_sim)
        bms.status.connect(lambda s: print("[bms]", s))

    win = MainWindow(link, "Hydrofoil" + (" (sim)" if args.sim else ""), bms)
    link.start_link()
    if bms is not None:
        bms.start_link()

    win.show() if args.windowed else win.showFullScreen()
    code = app.exec()
    if bms is not None:
        bms.stop_link()
    link.stop_link()
    return code
