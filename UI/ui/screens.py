# -*- coding: utf-8 -*-
"""screens.py — les ecrans. Ils lisent config.py, ils ne codent rien en dur."""

import time

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QWidget, QFrame, QLabel, QPushButton, QVBoxLayout, QHBoxLayout, QGridLayout,
    QPlainTextEdit, QSizePolicy
)

from . import config as cfg
from .widgets import (Card, RowCard, SpeedCard, Badge, EscCard, MultiCard, ChargeCard,
                      FoilCard, set_status_style)
from .cruise_popup import CruisePopup
from .foils_popup import FoilsPopup

CENTER = Qt.AlignmentFlag.AlignCenter


def _close_button(slot) -> QPushButton:
    b = QPushButton("Close")
    b.setObjectName("ghost")
    b.setFixedSize(130, 52)
    b.clicked.connect(slot)
    return b


# Pages de telemetrie, dans l'ordre des onglets
PAGES = [("moteurs", "Moteurs"), ("xsens", "Xsens"), ("bms", "BMS"),
         ("foils", cfg.FOILS_PAGE_LABEL)]


class _FitLabel(QLabel):
    """Texte entier s'il tient, sinon sans le mot « consigne », sinon
    rien : jamais un bout de texte coupe. Avec 4 onglets il n'y a plus
    la place : la consigne est dans la carte Vitesse de la page Moteurs.
    Recalcule a chaque changement de texte et de largeur."""

    def __init__(self, text=""):
        super().__init__(text)
        self._full = text

    def set_full(self, text: str):
        self._full = text
        self._fit()

    def resizeEvent(self, e):
        super().resizeEvent(e)
        self._fit()

    def _fit(self):
        w, fm = self.width(), self.fontMetrics()
        for t in (self._full, self._full.replace("consigne ", "")):
            if fm.horizontalAdvance(t) <= w:
                if self.text() != t:
                    self.setText(t)
                return
        if self.text():
            self.setText("")


class _PageBar(QVBoxLayout):
    """Barre commune aux pages de telemetrie : onglets a gauche,
    consigne et etat (mode · run) a droite. En dessous, un bandeau
    pleine largeur n'apparait que pendant une alerte du Teensy
    (batterie 12V, temperatures) : visible quelle que soit la page."""

    def __init__(self, active: str, on_page):
        super().__init__()
        self.setSpacing(6)
        ligne = QHBoxLayout()
        ligne.setSpacing(8)
        for key, text in PAGES:
            b = QPushButton(text)
            b.setObjectName("tabOn" if key == active else "tab")
            b.setFixedSize(112, 50)
            b.clicked.connect(lambda _=False, k=key: on_page(k))
            ligne.addWidget(b)
        self.target = _FitLabel("--")
        self.target.setObjectName("barHint")
        # Si la place manque, c'est la consigne qui est rognee, jamais l'etat
        self.target.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        self.target.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        self.mode = Badge("--", True)
        ligne.addWidget(self.target, 1)
        ligne.addSpacing(10)
        ligne.addWidget(self.mode)
        self.addLayout(ligne)

        self.banniere = QLabel("")
        self.banniere.setObjectName("barWarn")
        self.banniere.setAlignment(CENTER)
        self.banniere.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Fixed)
        self.banniere.hide()
        self.addWidget(self.banniere)

    def update_data(self, d: dict):
        self.mode.set_state(cfg.mode_text(d), cfg.mode_ok(d))
        self._update_alerte(d)
        # Calibration du levier en cours (ex. lancee par la commande
        # secrete F/R) : on affiche la mesure a la place de la consigne.
        if cfg.calib_en_cours(d):
            self._set_target("CALIB " + cfg.calib_texte_mesure(d))
        else:
            self._set_target(cfg.target_text(d))

    def _update_alerte(self, d: dict):
        alerte = cfg.bandeau_alerte(d)
        if not alerte:
            self.banniere.hide()
            return
        texte, rouge = alerte
        name = "barAlert" if rouge else "barWarn"
        if self.banniere.objectName() != name:  # re-polish seulement si ca change
            self.banniere.setObjectName(name)
            self.banniere.style().unpolish(self.banniere)
            self.banniere.style().polish(self.banniere)
        self.banniere.setText(texte)
        self.banniere.show()

    def _set_target(self, text: str):
        self.target.set_full(text)


def _stop_button(slot) -> QPushButton:
    stop = QPushButton("Stop")
    stop.setObjectName("danger")
    stop.clicked.connect(slot)
    return stop


class StartScreen(QWidget):
    """Ecran d'accueil : titre centre, demarrage, theme, fermeture."""
    started = Signal(dict)
    quit_requested = Signal()
    theme_toggled = Signal()
    flash_requested = Signal()
    calib_requested = Signal()

    def __init__(self, payload: dict = None):
        super().__init__()
        self._payload = payload or cfg.START_PAYLOAD

        root = QVBoxLayout(self)
        root.setContentsMargins(22, 18, 22, 18)
        root.setSpacing(12)

        bar = QHBoxLayout()
        self.theme_btn = QPushButton()
        self.theme_btn.setObjectName("ghost")
        self.theme_btn.setFixedSize(170, 52)
        self.theme_btn.clicked.connect(self.theme_toggled.emit)
        bar.addWidget(self.theme_btn)
        flash = QPushButton("Mise a jour Teensy")
        flash.setObjectName("ghost")
        flash.setFixedSize(220, 52)
        flash.clicked.connect(self.flash_requested.emit)
        bar.addWidget(flash)
        calib = QPushButton("Calibration")
        calib.setObjectName("ghost")
        calib.setFixedSize(180, 52)
        calib.clicked.connect(self.calib_requested.emit)
        bar.addWidget(calib)
        bar.addStretch()
        bar.addWidget(_close_button(self.quit_requested.emit))
        root.addLayout(bar)

        root.addStretch(1)

        titre = QLabel("Memphre Boat Racing")
        titre.setObjectName("title")
        titre.setAlignment(CENTER)
        root.addWidget(titre)

        # Message d'alerte (ex. arret automatique batterie basse)
        self._msg = QLabel("")
        self._msg.setObjectName("startAlert")
        self._msg.setAlignment(CENTER)
        self._msg.setWordWrap(True)
        root.addWidget(self._msg)

        root.addStretch(2)

        go = QPushButton("Demarrer")
        go.setObjectName("primary")
        go.clicked.connect(lambda: self.started.emit(dict(self._payload)))
        root.addWidget(go)

    def set_message(self, text: str):
        self._msg.setText(text)

    def set_theme_label(self, current: str):
        """Le bouton annonce le theme vers lequel il bascule."""
        self.theme_btn.setText("Theme clair" if current == "sombre" else "Theme sombre")


class FlashScreen(QWidget):
    """Mise a jour du Teensy : avertissement, lancement, sortie de pio.
    Le bouton Lancer est une confirmation volontaire : le Teensy
    redemarre pendant l'operation, les moteurs s'arretent."""
    run_requested = Signal()
    back_requested = Signal()

    def __init__(self):
        super().__init__()
        root = QVBoxLayout(self)
        root.setContentsMargins(22, 18, 22, 18)
        root.setSpacing(10)

        bar = QHBoxLayout()
        titre = QLabel("Mise a jour Teensy")
        titre.setObjectName("title")
        bar.addWidget(titre)
        bar.addStretch()
        self._back = QPushButton("Retour")
        self._back.setObjectName("ghost")
        self._back.setFixedSize(130, 52)
        self._back.clicked.connect(self.back_requested.emit)
        bar.addWidget(self._back)
        root.addLayout(bar)

        hint = QLabel("Bateau a quai, helices hors de l'eau. Le Teensy redemarre : "
                      "les moteurs s'arretent pendant la mise a jour.")
        hint.setObjectName("hint")
        hint.setWordWrap(True)
        root.addWidget(hint)

        self._log = QPlainTextEdit()
        self._log.setObjectName("flashLog")
        self._log.setReadOnly(True)
        self._log.setMaximumBlockCount(500)
        root.addWidget(self._log, 1)

        self._state = QLabel("")
        self._state.setObjectName("flashBusy")
        self._state.setWordWrap(True)
        root.addWidget(self._state)

        self._go = QPushButton("Lancer la mise a jour")
        self._go.setObjectName("primary")
        self._go.clicked.connect(self.run_requested.emit)
        root.addWidget(self._go)

    def reset(self):
        self._log.clear()
        self._set_state("", "flashBusy")
        self.set_busy(False)

    def set_busy(self, busy: bool):
        self._go.setEnabled(not busy)
        self._back.setEnabled(not busy)

    def append(self, line: str):
        self._log.appendPlainText(line)

    def show_state(self, text: str):
        self._set_state(text, "flashBusy")

    def show_result(self, ok: bool, text: str):
        self._set_state(text, "flashOk" if ok else "flashKo")

    def _set_state(self, text, name):
        self._state.setText(text)
        self._state.setObjectName(name)
        self._state.style().unpolish(self._state)
        self._state.style().polish(self._state)


class TelemetryScreen(QWidget):
    """Page Moteurs : courant moteur / puissance, une carte par ESC,
    et en bas Stop, le timer (foils_control ou cruise), le bouton Foils
    et la case Cruise control. Cartes generees depuis cfg."""
    stopped = Signal()
    page_requested = Signal(str)
    cruise_requested = Signal(dict)     # commande a envoyer au Teensy
    foils_requested = Signal(dict)      # idem (bouton Foils)

    def __init__(self, metrics=None, esc_ids=None):
        super().__init__()
        metrics = metrics or cfg.TOP_METRICS
        esc_ids = esc_ids or cfg.ESC_IDS

        root = QVBoxLayout(self)
        root.setContentsMargins(16, 12, 16, 12)
        root.setSpacing(8)

        self._bar = _PageBar("moteurs", self.page_requested.emit)
        root.addLayout(self._bar)

        # Rangee du haut : courant moteur | puissance dans une carte (moitie gauche),
        # charge batterie du BMS (moitie droite).
        top = QHBoxLayout()
        top.setSpacing(8)
        self._metrics = list(metrics)
        self._power = MultiCard(self._metrics)
        self._charge = ChargeCard(cfg.CHARGE_LABEL, cfg.BMS_WARN_SOC, cfg.BMS_CAP_SOC,
                                  cfg.CHARGE_WARN_TEXT, cfg.CHARGE_CAP_TEXT)
        top.addWidget(self._power, 1)
        top.addWidget(self._charge, 1)
        root.addLayout(top)

        # Rangee du milieu : ESC | vitesse GPS | ESC (ordre de cfg.ESC_IDS)
        grid = QGridLayout()
        grid.setSpacing(8)
        self._esc_cards = {}
        self._temp_ok = {}      # (id ESC, cle) -> etat precedent, pour l'hysteresis
        self._batt_temp_ok = True   # idem pour la temperature batterie
        cols = [0, 2]           # colonne 1 = vitesse
        for n, i in enumerate(esc_ids):
            c = EscCard("ESC %d" % i, cfg.esc_unit(), len(cfg.ESC_TEMPS))
            grid.addWidget(c, n // 2, cols[n % 2])
            self._esc_cards[i] = c
        self._speed = SpeedCard(cfg.SPEED_LABEL_COURT, cfg.SPEED_UNIT, sub_label="Consigne")
        self._speed.setFixedWidth(220)
        grid.addWidget(self._speed, 0, 1, max(1, (len(esc_ids) + 1) // 2), 1)
        grid.setColumnStretch(0, 1)
        grid.setColumnStretch(2, 1)
        root.addLayout(grid, 1)     # les cartes ESC prennent la hauteur libre

        # Rangee du bas : Stop | timer (foils_control, sinon cruise) | Foils | Cruise control
        bas = QHBoxLayout()
        bas.setSpacing(8)
        stop = _stop_button(self.stopped.emit)
        stop.setFixedWidth(150)
        bas.addWidget(stop)
        self._cruise_mode = cfg.CRUISE_OFF      # etat renvoye par le Teensy
        self._cruise_appui = 0.0                # dernier choix (affichage d'un refus)
        self._dernier = {}                      # derniere trame (ouverture du pop-up)
        col = QVBoxLayout()
        col.setSpacing(0)
        self._cruise_timer = QLabel("--:--")
        self._cruise_timer.setObjectName("cruiseTimer")
        self._cruise_timer.setAlignment(CENTER)
        self._cruise_info = QLabel("")
        self._cruise_info.setObjectName("cruiseInfo")
        self._cruise_info.setAlignment(CENTER)
        # Largeur fixee par la rangee, jamais par le texte
        self._cruise_info.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        col.addWidget(self._cruise_timer)
        col.addWidget(self._cruise_info)
        bas.addLayout(col, 1)
        self._foils_appui = 0.0                 # dernier armement (affichage d'un refus)
        self._foils_btn = QPushButton("Foils")
        self._foils_btn.setObjectName("foils")
        self._foils_btn.setFixedWidth(170)
        self._foils_btn.setEnabled(False)
        self._foils_btn.clicked.connect(self._on_foils)
        bas.addWidget(self._foils_btn)
        self._cruise_btn = QPushButton("Cruise control")
        self._cruise_btn.setObjectName("cruise")
        self._cruise_btn.setFixedWidth(230)
        self._cruise_btn.setEnabled(False)
        self._cruise_btn.clicked.connect(self._on_cruise)
        bas.addWidget(self._cruise_btn)
        root.addLayout(bas)

        self._popup = CruisePopup(self)
        self._popup.choisi.connect(self._on_choix)
        self._foils_popup = FoilsPopup(self)
        self._foils_popup.confirme.connect(self._on_foils_confirme)

    def _on_foils(self):
        """Armé : désarmer tout de suite. Sinon : pop-up de confirmation."""
        if cfg.foils_arme(self._dernier):
            self.foils_requested.emit(dict(cfg.FOILS_OFF_PAYLOAD))
        else:
            self._popup.fermer()
            self._foils_popup.ouvrir(self._dernier)

    def _on_foils_confirme(self):
        self._foils_appui = time.monotonic()
        self.foils_requested.emit(dict(cfg.FOILS_ON_PAYLOAD))

    def _on_cruise(self):
        """Cruise actif : arret (la telemetrie continue). Sinon : pop-up
        de choix du mode."""
        if self._cruise_mode != cfg.CRUISE_OFF:
            self.cruise_requested.emit(dict(cfg.CRUISE_OFF_PAYLOAD))
        else:
            self._foils_popup.fermer()
            self._popup.ouvrir(self._dernier)

    def _on_choix(self, mode: int):
        self._cruise_appui = time.monotonic()
        self.cruise_requested.emit(dict(cfg.CRUISE_PAYLOADS[mode]))

    def _update_cruise(self, d: dict):
        self._dernier = d
        actif = cfg.cruise_mode(d)
        self._cruise_mode = actif
        b = self._cruise_btn
        b.setText(cfg.cruise_bouton_texte(d))
        # Actif : toujours cliquable (arret). Arrete : il faut RUN·FORWARD.
        b.setEnabled(actif != cfg.CRUISE_OFF or cfg.cruise_possible(d))
        self._set_name(b, "cruiseOn" if actif else "cruise")
        if actif:
            self._popup.fermer()
        else:
            self._popup.maj(d)
        self._cruise_timer.setText(cfg.cruise_timer_text(d))
        self._set_name(self._cruise_timer, "cruiseTimerOn" if actif else "cruiseTimer")
        rearme = cfg.cruise_attend_rearme(d)
        if actif:
            info = "appuyer sur ON pour arreter"
        elif rearme:
            info = cfg.CRUISE_TEXTE_REARME      # consigne a 0 jusque-la
        elif cfg.cruise_fin(d) in cfg.CRUISE_REFUS:
            # Un refus n'est affiche que juste apres le choix dans le pop-up
            recent = time.monotonic() - self._cruise_appui < cfg.CRUISE_REFUS_AFFICHE_S
            info = cfg.cruise_fin_text(d) if recent else ""
        else:
            info = cfg.cruise_fin_text(d)
        self._update_foils(d, actif, rearme, info)

    def _update_foils(self, d: dict, cruise_actif: int, rearme: bool, info_cruise: str):
        """Bouton Foils, et timer / info partagés avec le cruise : durée en
        foils_control dès que les foils sont actifs, sinon celle du cruise."""
        arme, ctl = cfg.foils_arme(d), cfg.foils_actif(d)
        b = self._foils_btn
        b.setText(cfg.foils_bouton_texte(d))
        b.setEnabled(arme or cfg.foils_possible(d))
        self._set_name(b, "foilsOn" if ctl else ("foilsArme" if arme else "foils"))
        self._foils_popup.maj(d)
        if ctl:
            self._cruise_timer.setText(cfg.foils_timer_text(d))
            self._set_name(self._cruise_timer, "cruiseTimerOn")
        info, alerte = info_cruise, rearme
        if not cruise_actif and not rearme:
            n = cfg.foils_fin(d)
            if ctl:
                info = "foils_control actif"
            elif arme:
                info = "Foils armé · " + cfg.foils_condition_court(d)
            elif n in cfg.FOILS_REFUS:
                if time.monotonic() - self._foils_appui < cfg.CRUISE_REFUS_AFFICHE_S:
                    info, alerte = cfg.foils_fin_text(d), True
            elif n and not info:
                info = cfg.foils_fin_text(d)
        self._cruise_info.setText(info)
        self._set_name(self._cruise_info, "cruiseInfoRearme" if alerte else "cruiseInfo")

    def hideEvent(self, ev):
        self._popup.fermer()                    # changement d'onglet, Stop
        self._foils_popup.fermer()
        super().hideEvent(ev)

    @staticmethod
    def _set_name(w, name: str):
        if w.objectName() != name:              # re-polish seulement si ca change
            w.setObjectName(name)
            w.style().unpolish(w)
            w.style().polish(w)

    def update_data(self, d: dict):
        self._bar.update_data(d)
        self._speed.set_value(cfg.speed_text(d))
        self._speed.set_sub(cfg.target_short(d))
        self._update_cruise(d)

        texts, alertes = [], []
        for m in self._metrics:
            try:
                texts.append(m.value(d))
                alertes.append(bool(m.alerte and m.alerte(d)))
            except Exception:
                texts.append("--")
                alertes.append(False)
        self._power.set_values(texts, alertes)
        self.update_bms(d)

        seen = set()
        for e in d.get("esc", []) or []:
            card = self._esc_cards.get(e.get("id"))
            if card is None:
                continue
            i = e.get("id")
            seen.add(i)
            if e.get("ok", 1):
                card.set_value(cfg.esc_value(e))
                card.set_unit(cfg.esc_unit())
                self._update_temps(i, card, e)
            else:
                self._esc_offline(i, card, cfg.esc_offline_sub(e))
        for i, card in self._esc_cards.items():
            if i not in seen:
                self._esc_offline(i, card, "absent de la trame")

    def update_bms(self, d: dict):
        """Barre de charge seule : appelee aussi quand le Teensy se tait.
        Sous le seuil, le texte dit si le Teensy a bien applique le plafond."""
        soc = cfg.bms_soc(d)
        txt = None
        if soc is not None and soc <= cfg.BMS_CAP_SOC:
            cap = cfg.bat_cap(d)
            if cap is None:
                txt = "Limite ?"           # ancien firmware : pas de confirmation
            elif not cap:
                txt = "Limite en attente"
            else:
                txt = "Limite %d %%" % cfg.BATT_BASSE_PCT
        self._charge.set_level(soc, txt)
        t = cfg.bms_temp(d)
        if t is None:
            self._batt_temp_ok = True
            self._charge.set_temp("%s : --" % cfg.BMS_TEMP_LABEL, None)
        else:
            self._batt_temp_ok = cfg.temp_ok(t, cfg.BMS_TEMP_MAX, self._batt_temp_ok)
            self._charge.set_temp(cfg.bms_temp_text(t, self._batt_temp_ok),
                                  self._batt_temp_ok)

    def _update_temps(self, esc_id, card, e: dict):
        for n, chk in enumerate(cfg.ESC_TEMPS):
            t = e.get(chk.key)
            if t is None:
                card.set_status(n, "%s : --" % chk.label, None)
                continue
            niveau = cfg.temp_niveau(e, n)
            if niveau is not None:          # le Teensy decide (config.h)
                card.set_status(n, *cfg.temp_statut(chk.label, t, niveau))
                continue
            key = (esc_id, chk.key)         # ancien firmware : seuils de cfg
            ok = cfg.temp_ok(t, chk.limit, self._temp_ok.get(key, True))
            self._temp_ok[key] = ok
            card.set_status(n, cfg.temp_text(chk.label, t, ok), ok)

    def _esc_offline(self, esc_id, card, reason: str):
        card.set_value("--")
        card.set_unit("")
        card.clear_status()
        card.set_status(0, reason, None)    # raison sous le chiffre (place)
        for chk in cfg.ESC_TEMPS:
            self._temp_ok.pop((esc_id, chk.key), None)


class XsensScreen(QWidget):
    """Page Xsens : vitesse GPS en tres gros, angles, etat IMU / GPS."""
    stopped = Signal()
    page_requested = Signal(str)

    def __init__(self, metrics=None):
        super().__init__()
        metrics = metrics or cfg.IMU_METRICS

        root = QVBoxLayout(self)
        root.setContentsMargins(16, 12, 16, 12)
        root.setSpacing(8)

        self._bar = _PageBar("xsens", self.page_requested.emit)
        root.addLayout(self._bar)

        # Vitesse (gauche, large) + angles (droite, une ligne chacun)
        main = QHBoxLayout()
        main.setSpacing(8)
        self._speed = Card(cfg.SPEED_LABEL, cfg.SPEED_UNIT, "speedValue",
                           center=True, unit_name="speedUnit")
        main.addWidget(self._speed, 1)

        # Colonne des angles a largeur fixe : la mise en page ne bouge
        # pas quand les valeurs changent de longueur.
        colw = QWidget()
        colw.setFixedWidth(280)
        col = QVBoxLayout(colw)
        col.setContentsMargins(0, 0, 0, 0)
        col.setSpacing(8)
        self._metrics = []
        for m in metrics:
            c = RowCard(m.label, m.unit)
            col.addWidget(c, 1)
            self._metrics.append((m, c))
        main.addWidget(colw)
        root.addLayout(main, 1)

        # Ligne du bas : etats IMU / GPS, position, altitude
        info = QFrame()
        info.setObjectName("card")
        lay = QHBoxLayout(info)
        lay.setContentsMargins(12, 6, 12, 6)
        lay.setSpacing(12)
        self._imu_state = QLabel("--")
        self._gps_state = QLabel("--")
        for w in (self._imu_state, self._gps_state):
            set_status_style(w, None)
            lay.addWidget(w)
        lay.addStretch()
        self._pos = QLabel("--");  self._pos.setObjectName("smallValue")
        self._alt = QLabel("--");  self._alt.setObjectName("smallValue")
        lay.addWidget(self._pos)
        lay.addSpacing(10)
        lay.addWidget(self._alt)
        root.addWidget(info)

        root.addWidget(_stop_button(self.stopped.emit))

    def update_data(self, d: dict):
        self._bar.update_data(d)
        self._speed.set_value(cfg.speed_text(d))
        for m, card in self._metrics:
            try:
                card.set_value(m.value(d))
            except Exception:
                card.set_value("--")
        for w, (text, ok) in ((self._imu_state, cfg.imu_status(d)),
                              (self._gps_state, cfg.gps_status(d))):
            w.setText(text)
            set_status_style(w, ok)
        self._pos.setText(cfg.position_text(d))
        self._alt.setText(cfg.altitude_text(d))


class BmsScreen(QWidget):
    """Page BMS : charge en tres gros, tension / courant / cellules /
    temperature, etat et alarmes. Donnees lues en Bluetooth par le Pi."""
    stopped = Signal()
    page_requested = Signal(str)

    def __init__(self, metrics=None):
        super().__init__()
        metrics = metrics or cfg.BMS_METRICS

        root = QVBoxLayout(self)
        root.setContentsMargins(16, 12, 16, 12)
        root.setSpacing(8)

        self._bar = _PageBar("bms", self.page_requested.emit)
        root.addLayout(self._bar)

        main = QHBoxLayout()
        main.setSpacing(8)
        self._soc = Card(cfg.BMS_SOC.label, cfg.BMS_SOC.unit, "speedValue",
                         center=True, unit_name="speedUnit")
        main.addWidget(self._soc, 1)

        colw = QWidget()
        colw.setFixedWidth(350)
        col = QVBoxLayout(colw)
        col.setContentsMargins(0, 0, 0, 0)
        col.setSpacing(6)
        self._metrics = []
        for m in metrics:
            c = RowCard(m.label, m.unit)
            # 6 lignes (avec la batterie 12V) : valeur plus petite pour
            # tenir dans la hauteur de la ligne (50 px ailleurs)
            c._value.setStyleSheet("font-size:34px;")
            col.addWidget(c, 1)
            self._metrics.append((m, c))
        main.addWidget(colw)
        root.addLayout(main, 1)

        info = QFrame()
        info.setObjectName("card")
        lay = QHBoxLayout(info)
        lay.setContentsMargins(12, 6, 12, 6)
        self._state = QLabel("--")
        set_status_style(self._state, None)
        lay.addWidget(self._state)
        lay.addStretch()
        self._info = QLabel("--")
        self._info.setObjectName("smallValue")
        lay.addWidget(self._info)
        root.addWidget(info)

        root.addWidget(_stop_button(self.stopped.emit))

    def update_data(self, d: dict):
        self._bar.update_data(d)
        self.update_bms(d)

    def update_bms(self, d: dict):
        """Partie BMS seule : appelee aussi quand le Teensy se tait."""
        for m, card in [(cfg.BMS_SOC, self._soc)] + self._metrics:
            try:
                card.set_value(m.value(d))
            except Exception:
                card.set_value("--")
        text, ok = cfg.bms_status(d)
        self._state.setText(text)
        set_status_style(self._state, ok)
        self._info.setText(cfg.bms_info(d))


class FoilsScreen(QWidget):
    """Page Foils : pour chaque foil, hauteur lue par le sonar, validite
    du sonar et commande du servo. En bas : etat du controle, et en gros
    le pitch et le roll du bateau (Xsens) avec la sortie de leur PID."""
    stopped = Signal()
    page_requested = Signal(str)

    def __init__(self, channels=None):
        super().__init__()
        channels = channels or cfg.FOIL_CHANNELS

        root = QVBoxLayout(self)
        root.setContentsMargins(16, 12, 16, 12)
        root.setSpacing(8)

        self._bar = _PageBar("foils", self.page_requested.emit)
        root.addLayout(self._bar)

        row = QHBoxLayout()
        row.setSpacing(8)
        self._cards = []
        for ch in channels:
            c = FoilCard(ch.label, cfg.SONAR_UNIT)
            row.addWidget(c, 1)
            self._cards.append((ch, c))
        root.addLayout(row, 1)

        info = QFrame()
        info.setObjectName("card")
        lay = QHBoxLayout(info)
        lay.setContentsMargins(12, 4, 12, 4)
        lay.setSpacing(16)
        self._state = QLabel("--")
        self._state.setWordWrap(True)
        set_status_style(self._state, None)
        lay.addWidget(self._state, 1)
        self._angles = {}
        for key, pid, nom in (("pitch", "p", "Pitch"), ("roll", "r", "Roll")):
            col = QVBoxLayout()
            col.setSpacing(0)
            haut = QHBoxLayout()
            lab = QLabel(nom)
            lab.setObjectName("cardLabel")
            sortie = QLabel("")
            sortie.setObjectName("hint")
            haut.addWidget(lab)
            haut.addStretch()
            haut.addWidget(sortie)
            val = QLabel("--")
            val.setObjectName("foilsAngle")
            val.setAlignment(CENTER)
            col.addLayout(haut)
            col.addWidget(val)
            lay.addLayout(col, 1)
            self._angles[key] = (pid, val, sortie)
        root.addWidget(info)

        root.addWidget(_stop_button(self.stopped.emit))

    def update_data(self, d: dict):
        self._bar.update_data(d)
        for ch, card in self._cards:
            card.set_value(cfg.sonar_text(d, ch.key))
            card.set_sonar(*cfg.sonar_status(d, ch.key))
            card.set_servo(cfg.servo_text(d, ch.key), cfg.servo_pct(d, ch.key))
        text, ok = cfg.foils_status(d)
        self._state.setText(text)
        set_status_style(self._state, ok)
        for key, (pid, val, sortie) in self._angles.items():
            val.setText(cfg.foils_angle_text(d, key))
            sortie.setText(cfg.foils_pid_text(d, pid))


class CalibScreen(QWidget):
    """Calibration du levier de vitesse.
    Commencer -> bouger le levier butee a butee -> le remettre au repos
    -> Terminer. Le Teensy enregistre min/max en EEPROM."""
    start_requested = Signal()
    finish_requested = Signal()
    cancel_requested = Signal()
    back_requested = Signal()

    def __init__(self):
        super().__init__()
        root = QVBoxLayout(self)
        root.setContentsMargins(22, 18, 22, 18)
        root.setSpacing(10)

        bar = QHBoxLayout()
        titre = QLabel("Calibration du levier")
        titre.setObjectName("title")
        bar.addWidget(titre)
        bar.addStretch()
        self._back = QPushButton("Retour")
        self._back.setObjectName("ghost")
        self._back.setFixedSize(130, 52)
        self._back.clicked.connect(self.back_requested.emit)
        bar.addWidget(self._back)
        root.addLayout(bar)

        hint = QLabel("Switch ON a OFF. Appuyer sur Commencer, pousser le levier "
                      "jusqu'en butee puis le ramener au repos, puis Terminer.")
        hint.setObjectName("hint")
        hint.setWordWrap(True)
        root.addWidget(hint)

        root.addStretch(1)
        self._mesure = QLabel("levier : --")
        self._mesure.setObjectName("smallValue")
        self._mesure.setAlignment(CENTER)
        root.addWidget(self._mesure)
        self._etat = QLabel("")
        self._etat.setAlignment(CENTER)
        self._etat.setWordWrap(True)
        set_status_style(self._etat, None)
        root.addWidget(self._etat)
        root.addStretch(1)

        row = QHBoxLayout()
        self._go = QPushButton("Commencer")
        self._go.setObjectName("primary")
        self._go.clicked.connect(self.start_requested.emit)
        self._fin = QPushButton("Terminer")
        self._fin.setObjectName("primary")
        self._fin.clicked.connect(self.finish_requested.emit)
        self._annule = QPushButton("Annuler")
        self._annule.setObjectName("danger")
        self._annule.clicked.connect(self.cancel_requested.emit)
        for b in (self._go, self._fin, self._annule):
            row.addWidget(b)
        root.addLayout(row)
        self._show_buttons(False)

    def _show_buttons(self, en_cours: bool):
        self._go.setEnabled(not en_cours)
        self._fin.setEnabled(en_cours)
        self._annule.setEnabled(en_cours)
        self._back.setEnabled(not en_cours)

    def reset(self):
        self._mesure.setText("levier : --")
        self._etat.setText("")
        set_status_style(self._etat, None)
        self._show_buttons(False)

    def update_data(self, d: dict):
        en_cours = cfg.calib_en_cours(d)
        self._show_buttons(en_cours)
        self._mesure.setText(cfg.calib_texte_mesure(d))
        if en_cours:
            auto = cfg.calib_auto(d)
            self._etat.setText("Calibration automatique : finit seule, laisser le levier au repos a la fin"
                               if auto else "Calibration en cours (LED du Teensy clignote)")
            set_status_style(self._etat, None)
        else:
            text, ok = cfg.calib_resultat(d)
            self._etat.setText(text)
            set_status_style(self._etat, ok)



class ServoCalibScreen(QWidget):
    """Calibration d'un servo des foils.
    - / + deplacent le servo (1 ou 10 µs, maintenir pour repeter) ;
    Enregistrer sauve la position comme Min / Neutre / Max dans l'EEPROM
    du Teensy. Terminer : le servo retourne a son neutre (s'il est valide)."""
    pas_requested = Signal(int)          # µs
    enregistrer_requested = Signal(str)  # "min" / "neutre" / "max"
    terminer_requested = Signal()

    def __init__(self):
        super().__init__()
        self.servo = 0
        root = QVBoxLayout(self)
        root.setContentsMargins(22, 14, 22, 14)
        root.setSpacing(8)

        bar = QHBoxLayout()
        self._titre = QLabel("Servo")
        self._titre.setObjectName("title")
        bar.addWidget(self._titre)
        bar.addStretch()
        fin = QPushButton("Terminer")
        fin.setObjectName("ghost")
        fin.setFixedSize(150, 52)
        fin.clicked.connect(self.terminer_requested.emit)
        bar.addWidget(fin)
        root.addLayout(bar)

        hint = QLabel("Switch ON coupé. Avancer doucement vers chaque butée, s'arrêter au "
                      "premier contact et reculer de 30 à 50 µs avant d'enregistrer.")
        hint.setObjectName("hint")
        hint.setWordWrap(True)
        root.addWidget(hint)

        self._pos = QLabel("-- µs")
        self._pos.setObjectName("escValue")
        self._pos.setAlignment(CENTER)
        root.addWidget(self._pos, 1)
        self._deg = QLabel("")
        self._deg.setObjectName("hint")
        self._deg.setAlignment(CENTER)
        root.addWidget(self._deg)

        pas = QHBoxLayout()
        pas.setSpacing(10)
        self._pas = []
        for d in (-cfg.SERVO_PAS_GROS, -cfg.SERVO_PAS_FIN, cfg.SERVO_PAS_FIN, cfg.SERVO_PAS_GROS):
            b = QPushButton("%+d" % d)
            b.setObjectName("servoPas")
            b.setAutoRepeat(True)               # maintenir = repeter
            b.setAutoRepeatDelay(400)
            b.setAutoRepeatInterval(100)
            b.clicked.connect(lambda _=False, v=d: self.pas_requested.emit(v))
            pas.addWidget(b, 1)
            self._pas.append(b)
        root.addLayout(pas)

        enr = QHBoxLayout()
        enr.setSpacing(10)
        self._enr = []
        for k, lab in cfg.SERVO_VALEURS:
            b = QPushButton()
            b.setObjectName("servoEnr")
            b.clicked.connect(lambda _=False, v=k: self.enregistrer_requested.emit(v))
            enr.addWidget(b, 1)
            self._enr.append((b, lab))
        root.addLayout(enr)

        self._etat = QLabel("")
        self._etat.setAlignment(CENTER)
        self._etat.setWordWrap(True)
        set_status_style(self._etat, None)
        root.addWidget(self._etat)
        self._actif(False)

    def ouvrir(self, i: int):
        self.servo = i
        self._titre.setText("Calibration servo " + cfg.SERVO_NOMS[i])
        self._pos.setText("-- µs")
        self._deg.setText("")
        self._etat.setText("En attente du Teensy...")
        set_status_style(self._etat, None)
        for b, lab in self._enr:
            b.setText("Enregistrer %s\n--" % lab)
        self._actif(False)

    def _actif(self, oui: bool):
        for b in self._pas:
            b.setEnabled(oui)
        for b, _ in self._enr:
            b.setEnabled(oui)

    def update_data(self, d: dict):
        i = self.servo
        self._actif(cfg.servo_calib_actif(d) == i)
        self._pos.setText(cfg.servo_pos_text(d, i))
        self._deg.setText(cfg.servo_pos_deg_text(d, i))
        for n, (b, lab) in enumerate(self._enr):
            b.setText("Enregistrer %s\n%s" % (lab, cfg.servo_valeur_text(d, i, n)))
        texte, ok = cfg.servo_calib_message(d, i)
        self._etat.setText(texte)
        set_status_style(self._etat, ok)


class SonarZeroScreen(QWidget):
    """Zero des sonars, sur l'eau : bateau au repos, switch ON coupe.
    Mesurer -> le Teensy fait la moyenne de chaque sonar pendant 5 s et
    l'enregistre en EEPROM. Consigne de vol = zero + hauteur de vol."""
    mesurer_requested = Signal()
    terminer_requested = Signal()

    def __init__(self):
        super().__init__()
        root = QVBoxLayout(self)
        root.setContentsMargins(22, 14, 22, 14)
        root.setSpacing(8)

        bar = QHBoxLayout()
        titre = QLabel("Zéro des sonars")
        titre.setObjectName("title")
        bar.addWidget(titre)
        bar.addStretch()
        fin = QPushButton("Terminer")
        fin.setObjectName("ghost")
        fin.setFixedSize(150, 52)
        fin.clicked.connect(self.terminer_requested.emit)
        bar.addWidget(fin)
        root.addLayout(bar)

        hint = QLabel("Bateau à l'eau, au repos, switch ON coupé. Mesurer : moyenne "
                      "de 5 s de chaque sonar, enregistrée dans le Teensy.")
        hint.setObjectName("hint")
        hint.setWordWrap(True)
        root.addWidget(hint)

        cartes = QHBoxLayout()
        cartes.setSpacing(8)
        self._cartes = []
        for nom in cfg.SONAR_NOMS:
            f = QFrame()
            f.setObjectName("card")
            lay = QVBoxLayout(f)
            lay.setContentsMargins(12, 6, 12, 8)
            lay.setSpacing(2)
            lab = QLabel(nom.capitalize()); lab.setObjectName("cardLabel")
            dist = QLabel("--");            dist.setObjectName("multiValue")
            zero = QLabel("zéro : --");     zero.setObjectName("smallValue")
            haut = QLabel("hauteur : --");  haut.setObjectName("hint")
            vol = QLabel("vol : --");       vol.setObjectName("hint")
            res = QLabel("")
            for w in (lab, dist, zero, haut, vol, res):
                lay.addWidget(w)
            set_status_style(res, None)
            cartes.addWidget(f, 1)
            self._cartes.append((dist, zero, haut, vol, res))
        root.addLayout(cartes, 1)

        self._go = QPushButton("Mesurer le zéro (5 s)")
        self._go.setObjectName("primary")
        self._go.clicked.connect(self.mesurer_requested.emit)
        root.addWidget(self._go)

        self._etat = QLabel("")
        self._etat.setAlignment(CENTER)
        self._etat.setWordWrap(True)
        set_status_style(self._etat, None)
        root.addWidget(self._etat)

    def ouvrir(self):
        for dist, zero, haut, vol, res in self._cartes:
            dist.setText("--"); zero.setText("zéro : --"); haut.setText("hauteur : --")
            vol.setText("vol : --"); res.setText(""); set_status_style(res, None)
        self._go.setEnabled(True)
        self._go.setText("Mesurer le zéro (5 s)")
        self._etat.setText("En attente du Teensy...")
        set_status_style(self._etat, None)

    def update_data(self, d: dict):
        for i, (dist, zero, haut, vol, res) in enumerate(self._cartes):
            dist.setText(cfg.sonar_distance_text(d, i))
            zero.setText(cfg.sonar_zero_text(d, i))
            haut.setText(cfg.sonar_hauteur_text(d, i))
            vol.setText(cfg.sonar_vol_text(d, i))
            t, ok = cfg.sonar_res_text(d, i)
            res.setText(t)
            set_status_style(res, ok)
        en_cours = cfg.sonar_zero_actif(d)
        self._go.setEnabled(not en_cours and "sz" in d)
        self._go.setText("Mesure en cours..." if en_cours else "Mesurer le zéro (5 s)")
        texte, ok = cfg.sonar_zero_message(d)
        self._etat.setText(texte)
        set_status_style(self._etat, ok)
