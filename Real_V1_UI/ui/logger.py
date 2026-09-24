# -*- coding: utf-8 -*-
"""
logger.py — enregistre la telemetrie sur la carte SD, un fichier par session.

Une session = de l'appui sur Demarrer a l'appui sur Stop.
Format JSON Lines : une trame par ligne, telle que recue du Teensy,
avec en plus "t_pi" (secondes depuis le debut de la session).

Pourquoi JSONL plutot que CSV : si le Teensy ajoute des champs a la trame
(sonars, roll, pitch...), ils sont enregistres sans toucher a ce fichier.

Protection coupure de courant : flush + fsync toutes les FLUSH_S secondes.
Au pire, on perd la derniere seconde de donnees, jamais le fichier entier.
"""

import glob
import json
import os
import re
import time

FLUSH_S = 1.0


class TelemetryLogger:
    def __init__(self, folder: str):
        self._folder = os.path.expanduser(folder)
        self._f = None
        self._t0 = 0.0
        self._last_flush = 0.0
        self.path = None
        self.rows = 0

    @property
    def active(self) -> bool:
        return self._f is not None

    def start(self) -> str:
        """Ouvre un nouveau fichier de session et retourne son chemin."""
        self.stop()
        os.makedirs(self._folder, exist_ok=True)
        # Le numero de session garde l'ordre meme si l'horloge du Pi est
        # fausse (pas de RTC, pas d'internet sur l'eau).
        n = self._next_index()
        stamp = time.strftime("%Y%m%d-%H%M%S")
        self.path = os.path.join(self._folder, "session_%04d_%s.jsonl" % (n, stamp))
        self._f = open(self.path, "w", encoding="utf-8")
        self._t0 = time.monotonic()
        self._last_flush = self._t0
        self.rows = 0
        return self.path

    def write(self, frame: dict) -> None:
        if self._f is None:
            return
        now = time.monotonic()
        rec = {"t_pi": round(now - self._t0, 3)}
        rec.update(frame)
        try:
            self._f.write(json.dumps(rec, separators=(",", ":")) + "\n")
            self.rows += 1
            if now - self._last_flush >= FLUSH_S:
                self._sync()
                self._last_flush = now
        except OSError as e:
            # Carte pleine ou retiree : on arrete d'ecrire, l'UI continue.
            print("[log] ecriture impossible, enregistrement arrete:", e)
            self._close()

    def stop(self) -> None:
        if self._f is None:
            return
        try:
            self._sync()
        except OSError:
            pass
        self._close()
        print("[log] %d trames -> %s" % (self.rows, self.path))

    # ------------------------------------------------------------ interne
    def _sync(self):
        self._f.flush()
        os.fsync(self._f.fileno())

    def _close(self):
        try:
            self._f.close()
        except Exception:
            pass
        self._f = None

    def _next_index(self) -> int:
        best = 0
        for p in glob.glob(os.path.join(self._folder, "session_*.jsonl")):
            m = re.match(r"session_(\d+)_", os.path.basename(p))
            if m:
                best = max(best, int(m.group(1)))
        return best + 1
