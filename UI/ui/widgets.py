# -*- coding: utf-8 -*-
"""widgets.py — briques reutilisables, sans logique metier."""

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QFrame, QLabel, QVBoxLayout, QHBoxLayout, QSizePolicy,
                               QProgressBar)

CENTER = Qt.AlignmentFlag.AlignCenter
BOTTOM = Qt.AlignmentFlag.AlignBottom
RIGHT_V = Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter


def set_status_style(label: QLabel, ok):
    """ok = True (vert), False (rouge) ou None (pas de donnee)."""
    name = {True: "statusOk", False: "statusKo", None: "statusNone"}[ok]
    if label.objectName() != name:          # re-polish seulement si ca change
        label.setObjectName(name)
        label.style().unpolish(label)
        label.style().polish(label)


def _value_row(value: QLabel, unit: QLabel, center: bool = False) -> QHBoxLayout:
    """Valeur + unite sur la meme ligne, unite calee en bas du chiffre :
    la hauteur gagnee va aux chiffres."""
    row = QHBoxLayout()
    row.setSpacing(8)
    if center:
        row.addStretch()
    row.addWidget(value, 0, BOTTOM)
    row.addWidget(unit, 0, BOTTOM)
    row.addStretch()
    return row


class Card(QFrame):
    """Etiquette, puis grande valeur avec son unite a cote."""

    def __init__(self, label: str, unit: str = "", value_name: str = "cardValue",
                 center: bool = False, unit_name: str = "cardUnit"):
        super().__init__()
        self.setObjectName("card")
        lay = QVBoxLayout(self)
        lay.setContentsMargins(14, 6, 14, 6)
        lay.setSpacing(0)

        self._label = QLabel(label);  self._label.setObjectName("cardLabel")
        self._value = QLabel("--");   self._value.setObjectName(value_name)
        self._unit = QLabel(unit);    self._unit.setObjectName(unit_name)
        lay.addWidget(self._label)
        if center:                              # grande carte : valeur au milieu
            lay.addStretch()
        lay.addLayout(_value_row(self._value, self._unit, center))
        if center:
            lay.addStretch()

    def set_label(self, text): self._label.setText(text)
    def set_value(self, text): self._value.setText(text)
    def set_unit(self, text):  self._unit.setText(text)
    set_sub = set_unit                      # ancien nom, garde pour compatibilite


class SpeedCard(QFrame):
    """Etiquette, grande valeur centree, unite centree en dessous
    (carte etroite : l'unite ne tient pas a cote du chiffre)."""

    def __init__(self, label: str, unit: str, value_name: str = "speedMid"):
        super().__init__()
        self.setObjectName("card")
        lay = QVBoxLayout(self)
        lay.setContentsMargins(10, 6, 10, 8)
        lay.setSpacing(0)
        self._label = QLabel(label); self._label.setObjectName("cardLabel")
        self._value = QLabel("--");  self._value.setObjectName(value_name)
        self._unit = QLabel(unit);   self._unit.setObjectName("speedUnit")
        self._value.setAlignment(CENTER)
        self._unit.setAlignment(CENTER)
        lay.addWidget(self._label)
        lay.addStretch()
        lay.addWidget(self._value)
        lay.addWidget(self._unit)
        lay.addStretch()

    def set_value(self, text): self._value.setText(text)


class RowCard(QFrame):
    """Une ligne : etiquette a gauche, grande valeur + unite a droite."""

    def __init__(self, label: str, unit: str = ""):
        super().__init__()
        self.setObjectName("card")
        lay = QHBoxLayout(self)
        lay.setContentsMargins(16, 0, 16, 0)
        lay.setSpacing(8)

        self._label = QLabel(label); self._label.setObjectName("cardLabel")
        self._value = QLabel("--");  self._value.setObjectName("rowValue")
        self._unit = QLabel(unit);   self._unit.setObjectName("cardUnit")
        self._value.setAlignment(RIGHT_V)
        lay.addWidget(self._label)
        lay.addStretch()
        lay.addWidget(self._value)
        lay.addWidget(self._unit)

    def set_value(self, text): self._value.setText(text)


class EscCard(QFrame):
    """Carte moteur pour le pilote : grand RPM, puis une ligne par
    temperature (OK / NOT OK). Tout est centre, rien a dechiffrer."""

    def __init__(self, label: str, unit: str, n_status: int):
        super().__init__()
        self.setObjectName("card")
        # Largeur fixee par la grille, jamais par le texte : la mise en
        # page ne bouge pas quand une valeur change de longueur.
        self.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(14, 6, 14, 8)
        lay.setSpacing(2)

        # En-tete : nom de l'ESC a gauche, unite (ou raison hors ligne) a droite
        head = QHBoxLayout()
        self._label = QLabel(label); self._label.setObjectName("cardLabel")
        self._unit = QLabel(unit);   self._unit.setObjectName("cardLabel")
        head.addWidget(self._label)
        head.addStretch()
        head.addWidget(self._unit)
        lay.addLayout(head)

        self._value = QLabel("--");  self._value.setObjectName("escValue")
        self._value.setAlignment(CENTER)
        lay.addWidget(self._value, 1)

        self._status = []
        for _ in range(n_status):
            s = QLabel("--")
            s.setAlignment(CENTER)
            lay.addWidget(s, 0, CENTER)
            self._status.append(s)
        self.clear_status()

    def set_value(self, text): self._value.setText(text)
    def set_unit(self, text):  self._unit.setText(text)

    def set_status(self, i: int, text: str, ok):
        """ok = True (vert), False (rouge) ou None (pas de donnee)."""
        self._status[i].setText(text)
        set_status_style(self._status[i], ok)

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


class MultiCard(QFrame):
    """Une seule carte, plusieurs valeurs cote a cote (ex. V | A | W).
    Chaque colonne : petite etiquette, puis valeur + unite."""

    def __init__(self, metrics):
        super().__init__()
        self.setObjectName("card")
        self.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(14, 6, 14, 6)
        lay.setSpacing(6)
        self._values = []
        for n, m in enumerate(metrics):
            if n:
                lay.addStretch(1)
            col = QVBoxLayout()
            col.setSpacing(0)
            lab = QLabel(m.label);  lab.setObjectName("cardLabel")
            val = QLabel("--");     val.setObjectName("multiValue")
            unit = QLabel(m.unit);  unit.setObjectName("multiUnit")
            row = QHBoxLayout()
            row.setSpacing(4)
            row.addWidget(val, 0, BOTTOM)
            row.addWidget(unit, 0, BOTTOM)
            col.addWidget(lab)
            col.addLayout(row)
            lay.addLayout(col)
            self._values.append(val)

    def set_values(self, texts):
        for lab, t in zip(self._values, texts):
            lab.setText(t)


class ChargeCard(QFrame):
    """Charge batterie : pourcentage + barre qui se vide.
    Vert au-dessus de `warn`, jaune + texte d'alerte rouge a `warn` et
    moins, rouge + autre texte a `stop` et moins."""

    def __init__(self, label: str, warn: int, stop: int,
                 warn_text: str = "Attention", stop_text: str = "Vitesse limitee"):
        super().__init__()
        self.setObjectName("card")
        self.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        self._warn, self._stop = warn, stop
        self._warn_text, self._stop_text = warn_text, stop_text
        lay = QVBoxLayout(self)
        lay.setContentsMargins(14, 6, 14, 10)
        lay.setSpacing(4)

        head = QHBoxLayout()
        self._label = QLabel(label);  self._label.setObjectName("cardLabel")
        self._alert = QLabel("");     self._alert.setObjectName("chargeAlert")
        self._value = QLabel("--");   self._value.setObjectName("chargeValue")
        self._unit = QLabel("%");     self._unit.setObjectName("cardUnit")
        self._alert.setAlignment(CENTER)
        head.addWidget(self._label, 0, Qt.AlignmentFlag.AlignTop)
        head.addWidget(self._alert, 1, Qt.AlignmentFlag.AlignVCenter)
        head.addWidget(self._value, 0, BOTTOM)
        head.addWidget(self._unit, 0, BOTTOM)
        lay.addLayout(head)

        self._bar = QProgressBar()
        self._bar.setRange(0, 100)
        self._bar.setTextVisible(False)
        self._bar.setFixedHeight(22)
        lay.addWidget(self._bar)
        self.set_level(None)

    def set_level(self, pct, alert_override: str = None):
        """pct = 0..100, ou None si pas de donnee BMS.
        alert_override : texte rouge impose (ex. plafond non confirme)."""
        alert = ""
        if pct is None:
            self._value.setText("--")
            self._bar.setValue(0)
            name = "chargeNone"
        else:
            pct = max(0, min(100, int(round(pct))))
            self._value.setText("%d" % pct)
            self._bar.setValue(pct)
            if pct <= self._stop:
                name, alert = "chargeLow", self._stop_text
            elif pct <= self._warn:
                name, alert = "chargeMid", self._warn_text
            else:
                name = "chargeOk"
        self._alert.setText(alert_override if alert_override is not None else alert)
        if self._bar.objectName() != name:
            self._bar.setObjectName(name)
            self._bar.style().unpolish(self._bar)
            self._bar.style().polish(self._bar)
