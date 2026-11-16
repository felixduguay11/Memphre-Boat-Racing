#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Test du BMS ANT en Bluetooth, sans l'UI (PC ou Pi).

    pip install aiobmsble            # installe aussi bleak
    python3 bms_test.py --scan       # liste tout ce qui est autour
    python3 bms_test.py              # trouve le BMS ANT et lit 10 mesures
    python3 bms_test.py --address AA:BB:CC:DD:EE:FF --n 0   # en continu

Fermer l'app ANT BMS du telephone avant : une seule connexion a la fois.
"""

import argparse
import asyncio
import json
import sys
from enum import Enum

from bleak import BleakScanner
from aiobmsble import BMSConfig
from aiobmsble.utils import bms_cls, bms_identify



def to_plain(sample: dict) -> dict:
    """Meme conversion que ui/bms.py, copiee pour ne pas exiger PySide6."""
    def conv(v):
        if isinstance(v, Enum):
            return v.name
        if isinstance(v, (list, tuple)):
            return [conv(x) for x in v]
        if hasattr(v, "value") and not isinstance(v, (int, float, str, bool)):
            return float(v.value)
        return round(v, 4) if isinstance(v, float) else v
    return {k: conv(v) for k, v in sample.items()}


async def scan(timeout):
    print("Scan BLE %.0f s..." % timeout)
    found = await BleakScanner.discover(timeout=timeout, return_adv=True)
    for dev, adv in sorted(found.values(), key=lambda x: -(x[1].rssi or -999)):
        name = adv.local_name or dev.name or ""
        cls = await bms_identify(adv, dev.address)
        tag = "  <-- BMS %s" % cls.__module__.split(".")[-1] if cls else ""
        print("%s  %4s dBm  %s%s" % (dev.address, adv.rssi, name, tag))
    if not found:
        print("Rien. Bluetooth du PC active ?")


async def read(args):
    def match(dev, adv):
        if args.address:
            return dev.address.upper() == args.address.upper()
        return (adv.local_name or dev.name or "").startswith(args.prefix)

    print("Recherche de '%s'..." % (args.address or args.prefix))
    found = await BleakScanner.discover(timeout=args.timeout, return_adv=True)
    hits = [(d, a) for d, a in found.values() if match(d, a)]
    if not hits:
        print("Introuvable. BMS endormi (mettre sous charge / bouton) ? "
              "App du telephone encore connectee ?")
        return 1
    if len(hits) > 1:
        print("Plusieurs BMS correspondent, choisis le tien avec --address :")
        for d, a in sorted(hits, key=lambda x: -(x[1].rssi or -999)):
            print("   %s  %4s dBm  %s" % (d.address, a.rssi, a.local_name or d.name))
        return 1
    dev, adv = hits[0]
    cls = await bms_cls(args.type) if args.type else await bms_identify(adv, dev.address)
    cls = cls or await bms_cls("ant_bms")
    print("Trouve : %s (%s) -> %s" % (adv.local_name or dev.name, dev.address, cls.__module__))

    async with cls(ble_device=dev, config=BMSConfig(keep_alive=True, secret=args.secret)) as bms:
        k = 0
        while args.n == 0 or k < args.n:
            k += 1
            d = to_plain(await bms.async_update())
            if args.raw:
                print(json.dumps(d, ensure_ascii=False))
            else:
                c = d.get("cell_voltages") or []
                print("%2d  %5.1f V  %+6.1f A  SOC %s %%  cell %s-%s V  T %s °C" % (
                    k, d.get("voltage", 0), d.get("current", 0), d.get("battery_level", "?"),
                    "%.3f" % min(c) if c else "?", "%.3f" % max(c) if c else "?",
                    d.get("temp_values") or d.get("temperature", "?")))
            await asyncio.sleep(args.period)
    return 0


def main():
    ap = argparse.ArgumentParser(description="Test BMS ANT en BLE")
    ap.add_argument("--scan", action="store_true", help="lister les appareils BLE")
    ap.add_argument("--prefix", default="ANT", help="debut du nom Bluetooth")
    ap.add_argument("--address", default="", help="adresse MAC (prioritaire sur le nom)")
    ap.add_argument("--type", default="", help="ant_bms ou ant_leg_bms (defaut : auto)")
    ap.add_argument("--secret", default="", help="mot de passe BLE du BMS, si besoin")
    ap.add_argument("--n", type=int, default=10, help="nombre de lectures, 0 = infini")
    ap.add_argument("--period", type=float, default=1.0)
    ap.add_argument("--timeout", type=float, default=10.0)
    ap.add_argument("--raw", action="store_true", help="afficher le dict complet")
    args = ap.parse_args()
    try:
        if args.scan:
            asyncio.run(scan(args.timeout))
            return 0
        return asyncio.run(read(args))
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())
