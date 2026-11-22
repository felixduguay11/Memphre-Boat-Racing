# -*- coding: utf-8 -*-
"""calib_popup.py — pop-up « Calibration » de l'ecran d'accueil.

Choix : le levier, le zero des sonars (sur l'eau), un des 3 servos
des foils, ou Annuler."""

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import QFrame, QHBoxLayout, QLabel, QPushButton, QSizePolicy, QVBoxLayout

from . import config as cfg


class CalibPopup(QFrame):
    MARGE = 16
    levier = Signal()
    sonars = Signal()
    servo = Signal(int)             # 0 avant, 1 arriere gauche, 2 arriere droit

    def __init__(self, parent):
        super().__init__(parent)
        self.setObjectName("calibPopup")

        titre = QLabel("Calibration")
        titre.setObjectName("cruisePopupTitle")

        def gros(texte, action):
            b = QPushButton(texte)
            b.setObjectName("cruiseChoix")
            b.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
            b.clicked.connect(action)
            return b

        haut = QHBoxLayout()
        haut.setSpacing(12)
        haut.addWidget(gros("Levier", self._levier), 1)
        haut.addWidget(gros("Sonars\n(zéro sur l'eau)", self._sonars), 1)

        sous = QLabel("Servos des foils")
        sous.setObjectName("hint")
        servos = QHBoxLayout()
        servos.setSpacing(12)
        for i, nom in enumerate(cfg.SERVO_NOMS):
            court = nom.replace("arrière", "arr.")      # tient dans le bouton
            servos.addWidget(gros("Servo\n" + court, lambda _=False, n=i: self._servo(n)), 1)

        annuler = QPushButton("Annuler")
        annuler.setObjectName("cruiseAnnuler")
        annuler.clicked.connect(self.hide)

        lay = QVBoxLayout(self)
        lay.setContentsMargins(20, 14, 20, 16)
        lay.setSpacing(10)
        lay.addWidget(titre)
        lay.addLayout(haut, 2)
        lay.addWidget(sous)
        lay.addLayout(servos, 3)
        lay.addWidget(annuler)
        self.hide()

    def ouvrir(self):
        p = self.parentWidget()
        m = self.MARGE
        self.setGeometry(m, m, p.width() - 2 * m, p.height() - 2 * m)
        self.raise_()
        self.show()

    def _levier(self):
        self.hide()
        self.levier.emit()

    def _sonars(self):
        self.hide()
        self.sonars.emit()

    def _servo(self, i: int):
        self.hide()
        self.servo.emit(i)
