# -*- coding: utf-8 -*-
"""widgets.py — briques reutilisables, sans logique metier."""

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QFrame, QLabel, QVBoxLayout

CENTER = Qt.AlignmentFlag.AlignCenter


class Card(QFrame):
    """Etiquette + grande valeur + sous-titre."""

    def __init__(self, label: str, sub: str = ""):
        super().__init__()
        self.setObjectName("card")
        lay = QVBoxLayout(self)
        lay.setContentsMargins(14, 10, 14, 10)
        lay.setSpacing(2)

        self._label = QLabel(label);  self._label.setObjectName("cardLabel")
        self._value = QLabel("--");   self._value.setObjectName("cardValue")
        self._sub = QLabel(sub);      self._sub.setObjectName("cardSub")
        for w in (self._label, self._value, self._sub):
            lay.addWidget(w)

    def set_label(self, text): self._label.setText(text)
    def set_value(self, text): self._value.setText(text)
    def set_sub(self, text):   self._sub.setText(text)


class EscCard(QFrame):
    """Carte moteur pour le pilote : grand RPM, puis une ligne par
    temperature (OK / NOT OK). Tout est centre, rien a dechiffrer."""

    def __init__(self, label: str, unit: str, n_status: int):
        super().__init__()
        self.setObjectName("card")
        lay = QVBoxLayout(self)
        lay.setContentsMargins(14, 10, 14, 12)
        lay.setSpacing(4)

        self._label = QLabel(label); self._label.setObjectName("cardLabel")
        self._value = QLabel("--");  self._value.setObjectName("escValue")
        self._unit = QLabel(unit);   self._unit.setObjectName("cardSub")
        for w in (self._label, self._value, self._unit):
            w.setAlignment(CENTER)
            lay.addWidget(w)

        lay.addStretch()
        self._status = []
        for _ in range(n_status):
            s = QLabel("--")
            s.setAlignment(CENTER)
            lay.addWidget(s)
            self._status.append(s)
        self.clear_status()

    def set_value(self, text): self._value.setText(text)
    def set_unit(self, text):  self._unit.setText(text)

    def set_status(self, i: int, text: str, ok):
        """ok = True (vert), False (rouge) ou None (pas de donnee)."""
        s = self._status[i]
        s.setText(text)
        name = {True: "statusOk", False: "statusKo", None: "statusNone"}[ok]
        if s.objectName() != name:          # re-polish seulement si ca change
            s.setObjectName(name)
            s.style().unpolish(s)
            s.style().polish(s)

    def clear_status(self):
        for i in range(len(self._status)):
            self.set_status(i, "--", None)


class Badge(QLabel):
    """Pastille d'etat, verte ou rouge."""

    def __init__(self, text: str = "", ok: bool = False):
        super().__init__(text)
        self.set_state(text, ok)

    def set_state(self, text: str, ok: bool):
        self.setText(text)
        self.setObjectName("pill" if ok else "pillKo")
        self.style().unpolish(self)
        self.style().polish(self)
