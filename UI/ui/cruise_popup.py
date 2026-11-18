# -*- coding: utf-8 -*-
"""cruise_popup.py — pop-up « Cruise control » de la page Moteurs.

Ouvert par la case Cruise control quand le cruise est arrete. Trois
choix : ERPM (consigne fixe), vitesse GPS, ou Annuler. Les cibles
affichees viennent de la trame du Teensy (config.h). Se ferme seul
apres cfg.CRUISE_CHOIX_S secondes, ou des que le bateau n'est plus en
RUN·FORWARD (le Teensy refuserait la demande)."""

from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtWidgets import QFrame, QHBoxLayout, QLabel, QPushButton, QSizePolicy, QVBoxLayout

from . import config as cfg


class CruisePopup(QFrame):
    MARGE = 16
    choisi = Signal(int)            # cfg.CRUISE_ERPM ou cfg.CRUISE_VITESSE

    def __init__(self, parent):
        super().__init__(parent)
        self.setObjectName("cruisePopup")
        self._reste = 0

        titre = QLabel("Cruise control")
        titre.setObjectName("cruisePopupTitle")
        self._compte = QLabel("")
        self._compte.setObjectName("popupTimer")
        self._compte.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        haut = QHBoxLayout()
        haut.addWidget(titre)
        haut.addStretch(1)
        haut.addWidget(self._compte)

        self._btns = {}
        choix = QHBoxLayout()
        choix.setSpacing(12)
        for mode in (cfg.CRUISE_ERPM, cfg.CRUISE_VITESSE):
            b = QPushButton()
            b.setObjectName("cruiseChoix")
            # Les deux choix prennent toute la hauteur libre (gros boutons)
            b.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
            b.clicked.connect(lambda _=False, m=mode: self._choisir(m))
            choix.addWidget(b, 1)
            self._btns[mode] = b

        annuler = QPushButton("Annuler")
        annuler.setObjectName("cruiseAnnuler")
        annuler.clicked.connect(self.fermer)

        lay = QVBoxLayout(self)
        lay.setContentsMargins(20, 14, 20, 16)
        lay.setSpacing(12)
        lay.addLayout(haut)
        lay.addLayout(choix, 1)
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
        self._reste = cfg.CRUISE_CHOIX_S
        self._compte.setText("%d s" % self._reste)
        self._fin.start(cfg.CRUISE_CHOIX_S * 1000)
        self._sec.start()

    def maj(self, d: dict):
        """A chaque trame : cibles, bouton vitesse grise sans GPS, et
        fermeture si le cruise n'est plus possible."""
        if self.isVisible() and not cfg.cruise_possible(d, cfg.CRUISE_ERPM):
            self.fermer()
            return
        noms = {cfg.CRUISE_ERPM: "ERPM", cfg.CRUISE_VITESSE: "Vitesse GPS"}
        for mode, b in self._btns.items():
            ok = cfg.cruise_possible(d, mode)
            texte = "%s\n%s" % (noms[mode], cfg.cruise_cible(d, mode))
            if mode == cfg.CRUISE_VITESSE and not ok:
                texte += "\n(pas de GPS)"
            b.setText(texte)
            b.setEnabled(ok)

    def fermer(self):
        self._fin.stop()
        self._sec.stop()
        self.hide()

    # ------------------------------------------------------------ interne
    def _choisir(self, mode: int):
        self.fermer()
        self.choisi.emit(mode)

    def _tic(self):
        self._reste = max(0, self._reste - 1)
        self._compte.setText("%d s" % self._reste)
