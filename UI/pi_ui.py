#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
pi_ui.py — Interface tactile Raspberry Pi (800x480) pour l'hydrofoil.

Flux :
    1. Ecran "Reglage foils" : choix d'un preset PID
    2. Appui sur Demarrer -> envoi JSON au Teensy :
       {"cmd":"start","preset":"doux","pid":{"kp":0.4,"ki":0.1,"kd":0.2}}
    3. Ecran "Telemetrie" : affiche les trames JSON du Teensy (20 Hz)
    4. Appui sur Stop -> {"cmd":"stop"} et retour a l'ecran 1

Lancement :
    python3 pi_ui.py                 # UART reel
    python3 pi_ui.py --sim           # donnees simulees, sans Teensy
    python3 pi_ui.py --port /dev/ttyAMA0

Dependances :
    pip install PySide6 pyserial        # ou: sudo apt install python3-pyside6 python3-serial
"""

import sys
import json
import math
import time
import argparse

from PySide6.QtCore import Qt, QThread, Signal, QTimer, QMutex
from PySide6.QtGui import QFont
from PySide6.QtWidgets import (
    QApplication, QWidget, QLabel, QPushButton, QVBoxLayout, QHBoxLayout,
    QGridLayout, QStackedWidget, QFrame
)

try:
    import serial
except ImportError:
    serial = None

# ------------------------------------------------------------------ config
DEFAULT_PORT = "/dev/serial0"
BAUD = 115200
LINK_TIMEOUT_S = 1.0          # au-dela : lien considere perdu
POLE_PAIRS = 6

PRESETS = [
    ("Doux",     "doux",     0.4, 0.10, 0.2, "eau calme"),
    ("Normal",   "normal",   0.6, 0.05, 0.4, "par defaut"),
    ("Agressif", "agressif", 1.6, 0.00, 1.9, "vagues"),
]

# ------------------------------------------------------------------ style
QSS = """
QWidget        { background:#12161c; color:#e8edf4; font-family:"DejaVu Sans"; }
QLabel#title   { font-size:26px; font-weight:500; }
QLabel#hint    { font-size:15px; color:#93a1b3; }
QLabel#pill    { font-size:14px; padding:5px 14px; border-radius:14px;
                 background:#16362a; color:#57d9a3; }
QLabel#pillKo  { font-size:14px; padding:5px 14px; border-radius:14px;
                 background:#3a1c1c; color:#f08a8a; }
QFrame#card    { background:#1b212a; border-radius:10px; }
QLabel#cl      { font-size:14px; color:#93a1b3; }
QLabel#cv      { font-size:30px; font-weight:500; }
QLabel#cs      { font-size:14px; color:#93a1b3; }
QPushButton#preset {
    text-align:left; padding:12px 18px; border-radius:10px;
    border:1px solid #2c3542; background:#1b212a; font-size:19px;
}
QPushButton#preset:checked { border:2px solid #4a9eff; background:#15283d; }
QPushButton#go   { font-size:22px; border-radius:10px; border:2px solid #4a9eff;
                   background:#15283d; color:#8cc4ff; min-height:66px; }
QPushButton#stop { font-size:22px; border-radius:10px; border:2px solid #b44;
                   background:#331a1a; color:#ff9b9b; min-height:66px; }
QPushButton:pressed { background:#0e1218; }
"""


# ============================================================ lien serie
class SerialLink(QThread):
    """Lit les lignes JSON du Teensy dans un thread, emet un dict."""
    telemetry = Signal(dict)
    status = Signal(str)

    def __init__(self, port, baud):
        super().__init__()
        self._port_name = port
        self._baud = baud
        self._ser = None
        self._run = True
        self._lock = QMutex()

    def run(self):
        while self._run:
            if self._ser is None:
                try:
                    self._ser = serial.Serial(self._port_name, self._baud, timeout=0.2)
                    self.status.emit("ouvert")
                except Exception as e:
                    self.status.emit("erreur: %s" % e)
                    time.sleep(1.0)
                    continue
            try:
                raw = self._ser.readline()
            except Exception as e:
                self.status.emit("perdu: %s" % e)
                try:
                    self._ser.close()
                except Exception:
                    pass
                self._ser = None
                continue

            if not raw:
                continue
            line = raw.decode("utf-8", "ignore").strip()
            if not line.startswith("{"):
                continue          # "READY", "ACK", bruit -> ignore
            try:
                self.telemetry.emit(json.loads(line))
            except ValueError:
                pass

    def send(self, obj):
        payload = (json.dumps(obj, separators=(",", ":")) + "\n").encode()
        self._lock.lock()
        try:
            if self._ser is not None:
                self._ser.write(payload)
        except Exception:
            pass
        finally:
            self._lock.unlock()

    def stop(self):
        self._run = False
        self.wait(1000)
        if self._ser:
            self._ser.close()


# ============================================================ simulateur
class SimLink:
    """Remplace SerialLink pour tester l'UI sans Teensy."""
    def __init__(self, on_telemetry):
        self._n = 0
        self._on = on_telemetry
        self._running = False
        self._timer = QTimer()
        self._timer.timeout.connect(self._tick)
        self._timer.start(50)

    def send(self, obj):
        self._running = (obj.get("cmd") == "start")

    def _tick(self):
        if not self._running:
            return
        self._n += 1
        n = self._n
        erpm = 3200 + math.sin(n / 25.0) * 450
        vin = 71.0 + math.sin(n / 40.0) * 1.4
        iin = 18.0 + math.sin(n / 15.0) * 5.0
        self._on({
            "t": n * 50, "mode": "RUN", "run": "FORWARD",
            "target": int(erpm), "ramped": int(erpm),
            "esc": [
                {"id": 10, "ok": 1, "erpm": int(erpm), "duty": 0.42,
                 "i_mot": iin / 2 + 1, "i_in": iin / 2, "v_in": vin,
                 "t_fet": 42 + n / 400.0, "t_mot": 38 + n / 500.0},
                {"id": 11, "ok": 1, "erpm": int(erpm * 0.98), "duty": 0.41,
                 "i_mot": iin / 2, "i_in": iin / 2, "v_in": vin,
                 "t_fet": 41 + n / 400.0, "t_mot": 37 + n / 500.0},
            ],
        })

    def stop(self):
        self._timer.stop()


# ============================================================ widgets
def card(label, big="--", small=""):
    f = QFrame(); f.setObjectName("card")
    v = QVBoxLayout(f); v.setContentsMargins(14, 10, 14, 10); v.setSpacing(2)
    l = QLabel(label); l.setObjectName("cl")
    b = QLabel(big);   b.setObjectName("cv")
    s = QLabel(small); s.setObjectName("cs")
    v.addWidget(l); v.addWidget(b); v.addWidget(s)
    f.value, f.sub = b, s
    return f


class ConfigScreen(QWidget):
    started = Signal(dict)

    def __init__(self):
        super().__init__()
        root = QVBoxLayout(self)
        root.setContentsMargins(22, 18, 22, 18); root.setSpacing(12)

        bar = QHBoxLayout()
        t = QLabel("Reglage foils"); t.setObjectName("title")
        self.link = QLabel("Teensy hors ligne"); self.link.setObjectName("pillKo")
        bar.addWidget(t); bar.addStretch(); bar.addWidget(self.link)
        root.addLayout(bar)

        hint = QLabel("Choisis un jeu de gains, puis demarre."); hint.setObjectName("hint")
        root.addWidget(hint)

        self.buttons = []
        for name, key, kp, ki, kd, note in PRESETS:
            b = QPushButton("%s\nkp %.2f  ki %.2f  kd %.2f  —  %s" % (name, kp, ki, kd, note))
            b.setObjectName("preset"); b.setCheckable(True)
            b.preset = {"preset": key, "pid": {"kp": kp, "ki": ki, "kd": kd}}
            b.clicked.connect(lambda _, bb=b: self._select(bb))
            self.buttons.append(b); root.addWidget(b)
        self._select(self.buttons[0])

        root.addStretch()
        go = QPushButton("Demarrer"); go.setObjectName("go")
        go.clicked.connect(self._go)
        root.addWidget(go)

    def _select(self, btn):
        for b in self.buttons:
            b.setChecked(b is btn)
        self._sel = btn.preset

    def _go(self):
        msg = {"cmd": "start"}
        msg.update(self._sel)
        self.started.emit(msg)

    def set_link(self, ok):
        self.link.setText("Teensy connecte" if ok else "Teensy hors ligne")
        self.link.setObjectName("pill" if ok else "pillKo")
        self.link.setStyleSheet("")          # force le re-style


class TelemetryScreen(QWidget):
    stopped = Signal()

    def __init__(self):
        super().__init__()
        root = QVBoxLayout(self)
        root.setContentsMargins(22, 18, 22, 18); root.setSpacing(10)

        bar = QHBoxLayout()
        t = QLabel("Telemetrie"); t.setObjectName("title")
        self.mode = QLabel("--"); self.mode.setObjectName("pill")
        bar.addWidget(t); bar.addStretch(); bar.addWidget(self.mode)
        root.addLayout(bar)

        g = QGridLayout(); g.setSpacing(10)
        self.c_target = card("Consigne")
        self.c_vin    = card("Batterie")
        self.c_iin    = card("Courant")
        self.c_pw     = card("Puissance")
        for i, c in enumerate([self.c_target, self.c_vin, self.c_iin, self.c_pw]):
            g.addWidget(c, 0, i)
        root.addLayout(g)

        g2 = QGridLayout(); g2.setSpacing(10)
        self.esc = {10: card("ESC 10"), 11: card("ESC 11")}
        g2.addWidget(self.esc[10], 0, 0); g2.addWidget(self.esc[11], 0, 1)
        root.addLayout(g2)

        root.addStretch()
        st = QPushButton("Stop"); st.setObjectName("stop")
        st.clicked.connect(self.stopped.emit)
        root.addWidget(st)

    def update_data(self, d):
        self.mode.setText("%s · %s" % (d.get("mode", "?"), d.get("run", "?")))
        self.c_target.value.setText(str(int(d.get("target", 0))))
        self.c_target.sub.setText("eRPM")

        escs = d.get("esc", [])
        vin = max([e.get("v_in", 0.0) for e in escs] or [0.0])
        iin = sum(e.get("i_in", 0.0) for e in escs)
        self.c_vin.value.setText("%.1f" % vin);          self.c_vin.sub.setText("V")
        self.c_iin.value.setText("%.1f" % iin);          self.c_iin.sub.setText("A")
        self.c_pw.value.setText("%d" % round(vin * iin)); self.c_pw.sub.setText("W")

        for e in escs:
            c = self.esc.get(e.get("id"))
            if c is None:
                continue
            if not e.get("ok", 1):
                c.value.setText("--"); c.sub.setText("aucune trame CAN"); continue
            c.value.setText("%d" % round(e.get("erpm", 0) / POLE_PAIRS))
            c.sub.setText("RPM  ·  %.1f A  ·  FET %.0f °C  ·  mot %.0f °C"
                          % (e.get("i_mot", 0), e.get("t_fet", 0), e.get("t_mot", 0)))


# ============================================================ fenetre
class MainWindow(QStackedWidget):
    def __init__(self, link, simulated):
        super().__init__()
        self.link = link
        self.last_rx = 0.0

        self.cfg = ConfigScreen()
        self.tel = TelemetryScreen()
        self.addWidget(self.cfg); self.addWidget(self.tel)

        self.cfg.started.connect(self._start)
        self.tel.stopped.connect(self._stop)

        self.setFixedSize(800, 480)
        self.setWindowTitle("Hydrofoil" + (" (sim)" if simulated else ""))

        watchdog = QTimer(self)
        watchdog.timeout.connect(self._check_link)
        watchdog.start(300)

    def on_telemetry(self, d):
        self.last_rx = time.time()
        if self.currentWidget() is self.tel:
            self.tel.update_data(d)

    def _check_link(self):
        self.cfg.set_link(time.time() - self.last_rx < LINK_TIMEOUT_S)

    def _start(self, msg):
        print("-> Teensy:", json.dumps(msg))
        self.link.send(msg)
        self.setCurrentWidget(self.tel)

    def _stop(self):
        print("-> Teensy: {\"cmd\":\"stop\"}")
        self.link.send({"cmd": "stop"})
        self.setCurrentWidget(self.cfg)

    def keyPressEvent(self, e):
        if e.key() == Qt.Key.Key_Escape:
            self.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=DEFAULT_PORT)
    ap.add_argument("--sim", action="store_true", help="donnees simulees")
    ap.add_argument("--windowed", action="store_true", help="ne pas passer en plein ecran")
    args = ap.parse_args()

    app = QApplication(sys.argv)
    app.setStyleSheet(QSS)
    app.setFont(QFont("DejaVu Sans", 11))

    if args.sim or serial is None:
        win_link = None
        win = MainWindow(None, True)
        sim = SimLink(win.on_telemetry)
        win.link = sim
    else:
        win_link = SerialLink(args.port, BAUD)
        win = MainWindow(win_link, False)
        win_link.telemetry.connect(win.on_telemetry)
        win_link.status.connect(lambda s: print("[serial]", s))
        win_link.start()

    if args.windowed:
        win.show()
    else:
        win.showFullScreen()

    code = app.exec()
    if win_link:
        win_link.stop()
    sys.exit(code)


if __name__ == "__main__":
    main()
