# -*- coding: utf-8 -*-
"""
link.py — transport Pi <-> Teensy.

Toutes les implementations exposent la meme interface :
    .telemetry  Signal(dict)   trame JSON recue
    .status     Signal(str)    messages de diagnostic
    .send(obj)                 envoie un dict en JSON + '\\n'
    .start_link() / .stop_link()

Pour ajouter un transport (TCP, MQTT, USB...), sous-classer BaseLink.

MODE REEL : SerialLink est utilise par defaut. SimLink n'est plus
atteint que via --sim explicite (plus de bascule silencieuse si
pyserial manque : on veut savoir que le lien est mort).
"""

import json
import math
import time

from PySide6.QtCore import QObject, QThread, Signal, QTimer, QMutex

try:
    import serial
except ImportError:
    serial = None


class BaseLink(QObject):
    telemetry = Signal(dict)
    status = Signal(str)

    def send(self, obj: dict) -> None:
        raise NotImplementedError

    def start_link(self) -> None:
        pass

    def stop_link(self) -> None:
        pass


# ----------------------------------------------------------------- UART / USB
class SerialLink(BaseLink):
    """Lit les lignes JSON du Teensy dans un thread dedie."""

    def __init__(self, port: str, baud: int):
        super().__init__()
        self._port, self._baud = port, baud
        self._ser = None
        self._lock = QMutex()
        self._thread = QThread()
        self._run = False
        self.moveToThread(self._thread)
        self._thread.started.connect(self._loop)

    def start_link(self):
        self._run = True
        self._thread.start()

    def stop_link(self):
        self._run = False
        self._thread.quit()
        self._thread.wait(1000)
        if self._ser:
            try:
                self._ser.close()
            except Exception:
                pass

    def _loop(self):
        while self._run:
            if self._ser is None and not self._open():
                time.sleep(1.0)
                continue
            try:
                raw = self._ser.readline()
            except Exception as e:
                self.status.emit("lien perdu: %s" % e)
                self._close()
                continue
            if not raw:
                continue
            line = raw.decode("utf-8", "ignore").strip()
            if not line.startswith("{"):
                continue                      # READY, ACK, bruit -> ignore
            try:
                msg = json.loads(line)
            except ValueError:
                self.status.emit("json invalide: %s" % line[:60])
                continue
            if msg.get("type") == "hb":
                continue                      # heartbeat : pas de telemetrie
            self.telemetry.emit(msg)

    def _open(self) -> bool:
        try:
            self._ser = serial.Serial(self._port, self._baud, timeout=0.2)
            self.status.emit("port %s ouvert" % self._port)
            return True
        except Exception as e:
            self.status.emit("ouverture impossible: %s" % e)
            return False

    def _close(self):
        try:
            if self._ser:
                self._ser.close()
        except Exception:
            pass
        self._ser = None

    def send(self, obj: dict):
        payload = (json.dumps(obj, separators=(",", ":")) + "\n").encode()
        self._lock.lock()
        try:
            if self._ser is not None:
                self._ser.write(payload)
        except Exception as e:
            self.status.emit("envoi echoue: %s" % e)
        finally:
            self._lock.unlock()


# ----------------------------------------------------------- simulation
# Conservee : c'est le seul moyen de travailler l'UI sans bateau.
# Ne tourne plus que sur --sim.
class SimLink(BaseLink):
    """Fabrique des trames identiques a celles du Teensy, sans materiel."""

    def __init__(self, esc_ids, period_ms: int = 50):
        super().__init__()
        self._ids = list(esc_ids)
        self._n = 0
        self._active = False
        self._timer = QTimer(self)
        self._timer.setInterval(period_ms)
        self._timer.timeout.connect(self._tick)

    def start_link(self):
        self._timer.start()
        self.status.emit("mode simulation")

    def stop_link(self):
        self._timer.stop()

    def send(self, obj: dict):
        self._active = obj.get("cmd") == "start"
        self.status.emit("recu: %s" % json.dumps(obj))

    def _tick(self):
        if not self._active:
            return
        self._n += 1
        n = self._n
        erpm = 3200 + math.sin(n / 25.0) * 450
        vin = 71.0 + math.sin(n / 40.0) * 1.4
        iin = 18.0 + math.sin(n / 15.0) * 5.0
        escs = []
        for k, i in enumerate(self._ids):
            f = 1.0 - 0.02 * k
            escs.append({
                "id": i, "ok": 1, "erpm": int(erpm * f), "duty": 0.42 * f,
                "i_mot": iin / len(self._ids) + 1, "i_in": iin / len(self._ids),
                "v_in": vin, "t_fet": 42 + n / 400.0, "t_mot": 38 + n / 500.0,
            })
        self.telemetry.emit({"t": n * self._timer.interval(), "mode": "RUN",
                             "run": "FORWARD", "target": int(erpm),
                             "ramped": int(erpm), "esc": escs})


def make_link(port: str, baud: int, esc_ids, simulated: bool) -> BaseLink:
    if simulated:
        return SimLink(esc_ids)
    if serial is None:
        raise RuntimeError(
            "pyserial absent : pip install pyserial, ou lancer avec --sim")
    return SerialLink(port, baud)
