# -*- coding: utf-8 -*-
"""
bms.py — lecture du BMS ANT de la batterie en Bluetooth Low Energy.

Le BMS ne passe pas par le Teensy : le Pi s'y connecte directement.
Toutes les implementations exposent la meme interface :
    .sample     Signal(dict)   mesure du BMS (format plat, JSON)
    .status     Signal(str)    messages de diagnostic
    .start_link() / .stop_link()

Protocole : librairie aiobmsble (pip install aiobmsble), qui gere les
deux generations ANT :
    ant_bms      modeles recents (trame 7E A1 ... AA 55)
    ant_leg_bms  anciens modeles BLE (trame AA 55 AA FF)
Avec BMS_TYPE = "" le bon module est choisi d'apres l'annonce BLE.

ATTENTION : le BMS n'accepte qu'UNE connexion. L'app ANT BMS du
telephone doit etre fermee, sinon le Pi ne le trouve pas.
"""

import asyncio
import math
import threading
import time
from enum import Enum

from PySide6.QtCore import QObject, Signal, QTimer

try:
    from bleak import BleakScanner
    from aiobmsble import BMSConfig
    from aiobmsble.utils import bms_cls, bms_identify
except ImportError:              # l'UI doit demarrer meme sans BLE
    BleakScanner = None


def to_plain(sample: dict) -> dict:
    """BMSSample -> dict JSON (TempSensor -> float, Enum -> nom)."""
    def conv(v):
        if isinstance(v, Enum):
            return v.name
        if isinstance(v, (list, tuple)):
            return [conv(x) for x in v]
        if isinstance(v, dict):
            return {k: conv(x) for k, x in v.items()}
        if hasattr(v, "value") and not isinstance(v, (int, float, str, bool)):
            return float(v.value)            # TempSensor
        if isinstance(v, float):
            return round(v, 4)
        return v
    return {k: conv(v) for k, v in sample.items()}


class BaseBms(QObject):
    sample = Signal(dict)
    status = Signal(str)

    def start_link(self) -> None:
        pass

    def stop_link(self) -> None:
        pass


# ----------------------------------------------------------------- BLE reel
class BleBms(BaseBms):
    """Scan -> connexion -> lecture periodique, avec reconnexion auto.
    Tourne dans un thread qui a sa propre boucle asyncio : le thread Qt
    n'attend jamais le Bluetooth."""

    def __init__(self, name_prefix: str, address: str = "", bms_type: str = "",
                 period_s: float = 1.0, secret: str = "", scan_s: float = 8.0):
        super().__init__()
        self._prefix = name_prefix
        self._address = address.upper()
        self._type = bms_type
        self._period = period_s
        self._secret = secret
        self._scan_s = scan_s
        self._run = False
        self._thread = None

    def start_link(self):
        if BleakScanner is None:
            self.status.emit("BMS : bleak/aiobmsble absents (pip install aiobmsble)")
            return
        if self._thread and self._thread.is_alive():
            return
        self._run = True
        self._thread = threading.Thread(target=self._thread_main, name="bms", daemon=True)
        self._thread.start()

    def stop_link(self):
        self._run = False
        if self._thread:
            self._thread.join(timeout=3.0)

    def _thread_main(self):
        try:
            asyncio.run(self._main())
        except Exception as e:                       # ne doit jamais tuer l'UI
            self.status.emit("BMS : thread arrete (%s)" % e)

    def _match(self, dev, adv) -> bool:
        if self._address:
            return dev.address.upper() == self._address
        name = adv.local_name or dev.name or ""
        return name.startswith(self._prefix)

    async def _find(self):
        """(BLEDevice, classe BMS) ou (None, None)."""
        found = {}

        def cb(dev, adv):
            if dev.address not in found and self._match(dev, adv):
                found[dev.address] = (dev, adv)

        async with BleakScanner(cb):
            t0 = time.monotonic()
            # Par nom : on ecoute 3 s de plus apres le premier trouve, pour
            # voir s'il y en a plusieurs (competition : un BMS ANT par equipe).
            while self._run and time.monotonic() - t0 < self._scan_s:
                if found and (self._address or time.monotonic() - t0 > 3.0):
                    break
                await asyncio.sleep(0.2)
        if not found:
            return None, None
        if len(found) > 1:
            self.status.emit("BMS : %d appareils '%s' autour, fixer BMS_ADDRESS : %s" % (
                len(found), self._prefix,
                ", ".join("%s %s" % (d.address, a.local_name or d.name) for d, a in found.values())))
        dev, adv = max(found.values(), key=lambda x: x[1].rssi or -999)
        if self._type:
            cls = await bms_cls(self._type)
        else:
            cls = await bms_identify(adv, dev.address)
            if cls is None:                          # nom inconnu d'aiobmsble
                cls = await bms_cls("ant_bms")
        return dev, cls

    async def _sleep(self, s: float):
        t0 = time.monotonic()
        while self._run and time.monotonic() - t0 < s:
            await asyncio.sleep(0.1)

    async def _main(self):
        while self._run:
            self.status.emit("BMS : recherche de '%s'..." % (self._address or self._prefix))
            try:
                dev, cls = await self._find()
            except Exception as e:
                self.status.emit("BMS : scan impossible (%s)" % e)
                await self._sleep(5.0)
                continue
            if dev is None:
                self.status.emit("BMS : introuvable (endormi ? app du telephone connectee ?)")
                await self._sleep(2.0)
                continue
            if cls is None:
                self.status.emit("BMS : type '%s' inconnu d'aiobmsble" % self._type)
                return

            self.status.emit("BMS : %s (%s) -> %s" % (dev.name, dev.address, cls.__module__))
            try:
                async with cls(ble_device=dev,
                               config=BMSConfig(keep_alive=True, secret=self._secret)) as bms:
                    self.status.emit("BMS : connecte")
                    while self._run:
                        t0 = time.monotonic()
                        data = await asyncio.wait_for(bms.async_update(), timeout=10.0)
                        self.sample.emit(to_plain(data))
                        await self._sleep(max(0.0, self._period - (time.monotonic() - t0)))
            except Exception as e:
                self.status.emit("BMS : lien perdu (%s: %s)" % (type(e).__name__, e))
                await self._sleep(2.0)


# ----------------------------------------------------------- simulation
class SimBms(BaseBms):
    """Mesures plausibles d'un pack 18S Li-ion, sans materiel."""

    def __init__(self, period_s: float = 1.0, cells: int = 18,
                 soc0: float = 40.0, drain_pct_s: float = 0.5):
        super().__init__()
        # Decharge rapide : 40 % -> Attention (30 %) en ~20 s,
        # -> STOP (20 %) en ~40 s. Pour voir les alertes sans attendre.
        self._soc0, self._drain = soc0, drain_pct_s * period_s
        self._n = 0
        self._cells = cells
        self._timer = QTimer(self)
        self._timer.setInterval(int(period_s * 1000))
        self._timer.timeout.connect(self._tick)

    def start_link(self):
        self._timer.start()
        self.status.emit("BMS : simulation")

    def stop_link(self):
        self._timer.stop()

    def _tick(self):
        self._n += 1
        n = self._n
        cells = [round(3.70 + 0.002 * math.sin(n / 10.0 + k), 3) for k in range(self._cells)]
        v = sum(cells)
        i = -(35.0 + 15.0 * math.sin(n / 8.0))          # negatif = decharge
        self.sample.emit({
            "voltage": round(v, 2), "current": round(i, 1), "power": round(v * i, 1),
            "battery_level": max(0, int(self._soc0 - n * self._drain)), "cell_voltages": cells,
            "cell_count": self._cells, "delta_voltage": round(max(cells) - min(cells), 3),
            "temp_values": [24.0 + n / 200.0, 26.0 + n / 150.0],
            "temperature": 25.0 + n / 175.0, "cycle_charge": 80.0, "design_capacity": 100,
            "chrg_mosfet": True, "dischrg_mosfet": True, "problem": False, "problem_code": 0,
        })


def make_bms(cfg, simulated: bool) -> BaseBms:
    if simulated:
        return SimBms(cfg.BMS_PERIOD_S)
    return BleBms(cfg.BMS_NAME_PREFIX, cfg.BMS_ADDRESS, cfg.BMS_TYPE,
                  cfg.BMS_PERIOD_S, cfg.BMS_SECRET)
