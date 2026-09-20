# -*- coding: utf-8 -*-
"""widgets.py — briques reutilisables, sans logique metier."""

from PySide6.QtWidgets import QFrame, QLabel, QPushButton, QVBoxLayout, QHBoxLayout


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


class PresetButton(QPushButton):
    """Bouton radio d'un preset PID ; porte son payload."""

    def __init__(self, preset):
        super().__init__(preset.caption())
        self.setObjectName("preset")
        self.setCheckable(True)
        self.preset = preset
