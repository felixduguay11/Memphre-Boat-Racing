# -*- coding: utf-8 -*-
"""esc_popup.py — pop-up « Parametres ESC differents ».

Affiche, pour chaque parametre different, la valeur voulue (fichier de
reference) et la valeur reelle (lue dans l'ESC). Se ferme seul apres
cfg.ESC_PARAMS_POPUP_S secondes (le compte repart si de nouveaux ecarts
arrivent) ou avec le X dans le coin."""

from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import (
    QFrame, QLabel, QPushButton, QVBoxLayout, QHBoxLayout, QTableWidget,
    QTableWidgetItem, QHeaderView, QAbstractItemView
)

from . import config as cfg

COLONNES = ["ESC", "Parametre", "Voulu", "Reel"]


class EscParamsPopup(QFrame):
    MARGE = 16

    def __init__(self, parent):
        super().__init__(parent)
        self.setObjectName("popup")
        self._reste = 0

        titre = QLabel("Parametres ESC differents")
        titre.setObjectName("popupTitle")
        self._compte = QLabel("")
        self._compte.setObjectName("popupTimer")
        self._compte.setMinimumWidth(60)
        self._compte.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        fermer = QPushButton("✕")             # X
        fermer.setObjectName("popupClose")
        fermer.setFixedSize(56, 56)
        fermer.clicked.connect(self.fermer)

        haut = QHBoxLayout()
        haut.addWidget(titre)
        haut.addStretch(1)
        haut.addWidget(self._compte)
        haut.addSpacing(10)
        haut.addWidget(fermer)

        self._table = QTableWidget(0, len(COLONNES))
        self._table.setObjectName("popupTable")
        self._table.setHorizontalHeaderLabels(COLONNES)
        self._table.verticalHeader().setVisible(False)
        self._table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self._table.setSelectionMode(QAbstractItemView.SelectionMode.NoSelection)
        self._table.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self._table.setWordWrap(True)
        h = self._table.horizontalHeader()
        h.setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
        h.setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        h.setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
        h.setSectionResizeMode(3, QHeaderView.ResizeMode.ResizeToContents)

        lay = QVBoxLayout(self)
        lay.setContentsMargins(16, 12, 12, 12)
        lay.addLayout(haut)
        lay.addWidget(self._table, 1)

        self._fin = QTimer(self)
        self._fin.setSingleShot(True)
        self._fin.timeout.connect(self.fermer)
        self._sec = QTimer(self)
        self._sec.setInterval(1000)
        self._sec.timeout.connect(self._tic)
        self.hide()

    # ------------------------------------------------------------ API
    def ajouter(self, res: dict):
        """res : resultat de EscParamsChecker.traiter()."""
        esc = "ESC %d" % res["id"]
        if res.get("erreur"):
            self._ligne(esc, "%s : %s" % (res["nom"], res["erreur"]), "", "", erreur=True)
        for d in res.get("diffs", []):
            court = d["nom"].split("_", 1)[1] if "_" in d["nom"] else d["nom"]
            self._ligne(esc, "%s (%s)" % (d["libelle"], court), d["voulu"], d["reel"])
        self._afficher()

    def fermer(self):
        self._fin.stop()
        self._sec.stop()
        self.hide()
        self._table.setRowCount(0)

    # ------------------------------------------------------------ interne
    def _ligne(self, esc, param, voulu, reel, erreur=False):
        r = self._table.rowCount()
        self._table.insertRow(r)
        for c, txt in enumerate((esc, param, voulu, reel)):
            it = QTableWidgetItem(txt)
            if c >= 2:
                it.setTextAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            self._table.setItem(r, c, it)
        if erreur:
            self._table.setSpan(r, 1, 1, 3)
        self._table.resizeRowsToContents()

    def _afficher(self):
        p = self.parentWidget()
        m = self.MARGE
        self.setGeometry(m, m, p.width() - 2 * m, p.height() - 2 * m)
        self.raise_()
        self.show()
        self._reste = cfg.ESC_PARAMS_POPUP_S
        self._compte.setText("%d s" % self._reste)
        self._fin.start(cfg.ESC_PARAMS_POPUP_S * 1000)
        self._sec.start()

    def _tic(self):
        self._reste = max(0, self._reste - 1)
        self._compte.setText("%d s" % self._reste)
