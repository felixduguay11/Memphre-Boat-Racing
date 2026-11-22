# -*- coding: utf-8 -*-
"""foils_popup.py — pop-up « Contrôle des foils » de la page Moteurs.

Ouvert par le bouton Foils quand le contrôle n'est pas armé. Confirmer
envoie {"cmd":"foils_on"} : le Teensy passe en CONTROLE dès que les
conditions sont là (vitesse de config.h si la condition est active).
Annuler ferme. Se ferme seul après cfg.FOILS_CHOIX_S secondes, ou dès que
le bateau n'est plus en RUN (le Teensy refuserait)."""

from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtWidgets import QFrame, QHBoxLayout, QLabel, QPushButton, QSizePolicy, QVBoxLayout

from . import config as cfg


class FoilsPopup(QFrame):
    MARGE = 16
    confirme = Signal()

    def __init__(self, parent):
        super().__init__(parent)
        self.setObjectName("cruisePopup")      # même cadre que le pop-up du cruise
        self._reste = 0

        titre = QLabel("Contrôle des foils")
        titre.setObjectName("cruisePopupTitle")
        self._compte = QLabel("")
        self._compte.setObjectName("popupTimer")
        self._compte.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        haut = QHBoxLayout()
        haut.addWidget(titre)
        haut.addStretch(1)
        haut.addWidget(self._compte)

        self._texte = QLabel("")
        self._texte.setObjectName("foilsPopupTexte")
        self._texte.setWordWrap(True)
        self._texte.setAlignment(Qt.AlignmentFlag.AlignCenter)

        self._ok = QPushButton("Confirmer")
        self._ok.setObjectName("cruiseChoix")
        self._ok.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self._ok.clicked.connect(self._confirmer)

        annuler = QPushButton("Annuler")
        annuler.setObjectName("cruiseAnnuler")
        annuler.clicked.connect(self.fermer)

        lay = QVBoxLayout(self)
        lay.setContentsMargins(20, 14, 20, 16)
        lay.setSpacing(12)
        lay.addLayout(haut)
        lay.addWidget(self._texte)
        lay.addWidget(self._ok, 1)
        lay.addWidget(annuler)

        self._fin = QTimer(self)
        self._fin.setSingleShot(True)
        self._fin.timeout.connect(self.fermer)
        self._sec = QTimer(self)
        self._sec.setInterval(1000)
        self._sec.timeout.connect(self._tic)
        self.hide()

    # ------------------------------------------------------------ API
    def ouvrir(self, d: dict):
        p = self.parentWidget()
        m = self.MARGE
        self.setGeometry(m, m, p.width() - 2 * m, p.height() - 2 * m)
        self.maj(d)
        self.raise_()
        self.show()
        self._reste = cfg.FOILS_CHOIX_S
        self._compte.setText("%d s" % self._reste)
        self._fin.start(cfg.FOILS_CHOIX_S * 1000)
        self._sec.start()

    def maj(self, d: dict):
        """À chaque trame : texte (condition, PID actifs), et fermeture si
        armer n'est plus possible ou si c'est déjà armé."""
        if self.isVisible() and (not cfg.foils_possible(d) or cfg.foils_arme(d)):
            self.fermer()
            return
        self._texte.setText("Le contrôle des foils s'activera %s.\n%s"
                            % (cfg.foils_condition_text(d), cfg.foils_modes_text(d)))

    def fermer(self):
        self._fin.stop()
        self._sec.stop()
        self.hide()

    # ------------------------------------------------------------ interne
    def _confirmer(self):
        self.fermer()
        self.confirme.emit()

    def _tic(self):
        self._reste = max(0, self._reste - 1)
        self._compte.setText("%d s" % self._reste)
