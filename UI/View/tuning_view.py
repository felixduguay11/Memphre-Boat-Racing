from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QGridLayout,
                               QHBoxLayout, QLabel, QPushButton)

AXES = ["hauteur", "pitch", "roll"]
GAINS = ["kp", "ki", "kd"]


class TuningView(QWidget):
    def __init__(self):
        super().__init__()
        self.cells = {}

        racine = QVBoxLayout(self)
        grille = QGridLayout()
        grille.setSpacing(8)

        # En-têtes de colonnes
        for c, gain in enumerate(GAINS):
            entete = QLabel(gain.upper())
            entete.setAlignment(Qt.AlignmentFlag.AlignCenter)
            grille.addWidget(entete, 0, c + 1)

        # Noms d'axes + les 9 cellules
        for r, axe in enumerate(AXES):
            grille.addWidget(QLabel(axe.capitalize()), r + 1, 0)

            for c, gain in enumerate(GAINS):
                bouton = QPushButton("0.00")
                bouton.setMinimumHeight(60)
                self.cells[(axe, gain)] = bouton
                grille.addWidget(bouton, r + 1, c + 1)

        racine.addLayout(grille)
        racine.addStretch(1)

        # Rangée du bas
        bas = QHBoxLayout()
        self.btn_defauts = QPushButton("Défauts")
        self.btn_send = QPushButton("Send command")
        self.btn_defauts.setMinimumHeight(45)
        self.btn_send.setMinimumHeight(55)
        bas.addWidget(self.btn_defauts, 0)
        bas.addWidget(self.btn_send, 1)
        racine.addLayout(bas)