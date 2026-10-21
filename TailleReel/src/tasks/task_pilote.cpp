// =====================================================
//  task_pilote.cpp
//  Tâche FreeRTOS — entrées du pilote, 50 Hz.
//
//  Portage de Real_V1 :
//    - levier : bornes mesurées, deadband, mapping de consigne
//      (LireLevier, CreateTargetForward, CreateTargetReverse)
//    - switchs en pull-up : LOW = actif (SWITCH_ACTIF)
//  Ajouts :
//    - anti-rebond de chaque switch/bouton (SWITCH_ON/OFF_CYCLES)
//    - calibration du levier (bouton de l'UI du Pi), sauvée en EEPROM
//    - sortie DC_DC_3V3 : ON, coupée 3 s après I_But_Start ON
//    - commande secrète F/R → calibration automatique
//  Le volant (POT_Volant) sera ajouté ici plus tard.
// =====================================================

// ⚠ En PREMIER (comme FlexCAN_T4) : avant arduino_freertos.h
#include <avr/eeprom.h>

#include "task_pilote.h"
#include "task_watchdog.h"

// ─── Définition des variables partagées ───
PiloteData Pilote_data     = {};   // tout à false / 0
float      Pilote_temps_us = 0.0f;

// ─── Bornes du levier en service (calibration EEPROM ou config.h) ───
static int levMin = LEVIER_RAW_MIN;
static int levMax = LEVIER_RAW_MAX;

// map() d'Arduino (division entière), sans dépendre des macros
static long map_long(long x, long in_min, long in_max, long out_min, long out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Levier nettoyé : 0..1023 recalé sur les bornes réelles,
// et 0 franc tant qu'on est dans le deadband du repos.
// Une entrée flottante (pin non branchée) reste bruyante : c'est
// l'armement (machine d'état), pas le deadband, qui protège de ce cas.
static int LireLevier(int raw)
{
    if (raw <= levMin + LEVIER_DEADBAND) return 0;
    if (raw >= levMax) return 1023;
    return (int)map_long(raw, levMin, levMax, 0, 1023);
}

// Sous CMD_MIN_UTILE le moteur ne tourne pas proprement : dès que
// le levier sort du deadband on saute à CMD_MIN_UTILE, puis linéaire.
// (unité selon MODE_COMMANDE : eRPM / ‰ duty / 0,1 A)
static int CreateTargetForward(int lev)
{
    if (lev == 0) return 0;                 // repos : moteur arrêté
    return (int)map_long(lev, 1, 1023, CMD_MIN_UTILE, CMD_MAX_FORWARD);
}

static int CreateTargetReverse(int lev)
{
    if (lev == 0) return 0;                 // repos : moteur arrêté
    return -(int)map_long(lev, 1, 1023, CMD_MIN_UTILE, CMD_MAX_REVERSE);
}

// =====================================================
//  Anti-rebond : un changement n'est accepté qu'après
//  SWITCH_ON_CYCLES (vers actif) ou SWITCH_OFF_CYCLES (vers
//  inactif) lectures identiques de suite. Démarre inactif.
// =====================================================
struct Antirebond {
    bool    etat;
    uint8_t cpt;
};

static bool Filtrer(Antirebond &f, bool brut)
{
    if (brut == f.etat) { f.cpt = 0; return f.etat; }
    f.cpt++;
    uint8_t requis = brut ? SWITCH_ON_CYCLES : SWITCH_OFF_CYCLES;
    if (f.cpt >= requis) { f.etat = brut; f.cpt = 0; }
    return f.etat;
}

// =====================================================
//  Calibration du levier — sauvegarde EEPROM
// =====================================================
#define CALIB_MAGIC 0x4C455631UL   // "LEV1"

struct CalLevierEeprom {
    uint32_t magic;
    int16_t  min;
    int16_t  max;
    uint16_t somme;                // contrôle : min ^ max ^ 0xA5A5
};

static uint16_t CalSomme(int16_t mn, int16_t mx) { return (uint16_t)(mn ^ mx ^ 0xA5A5); }

static bool CalValide(int mn, int mx)
{
    return mn >= 0 && mx <= 1023 && (mx - mn) >= (CALIB_PLAGE_MIN - CALIB_MARGE_MAX);
}

static void CalCharger()
{
    CalLevierEeprom c;
    eeprom_read_block(&c, (const void *)CALIB_EEPROM_ADDR, sizeof(c));
    if (c.magic == CALIB_MAGIC && c.somme == CalSomme(c.min, c.max) && CalValide(c.min, c.max)) {
        levMin = c.min;
        levMax = c.max;
    }
    // sinon : valeurs de config.h
}

static void CalSauver(int mn, int mx)
{
    CalLevierEeprom c;
    c.magic = CALIB_MAGIC;
    c.min   = (int16_t)mn;
    c.max   = (int16_t)mx;
    c.somme = CalSomme(c.min, c.max);
    eeprom_write_block(&c, (void *)CALIB_EEPROM_ADDR, sizeof(c));
}

// =====================================================
//  Task_Pilote — 50 Hz
// =====================================================
void Task_Pilote(void *ptr)
{
    (void) ptr;

    pinMode(I_Switch_ON,   arduino::INPUT_PULLUP);
    pinMode(I_F_R,         arduino::INPUT_PULLUP);
    pinMode(Relay_Control, arduino::INPUT_PULLUP);
    // Fil débranché → niveau inactif (« ESC non démarrés »)
    pinMode(I_But_Start,   ESC_DEMARRES_ACTIF ? arduino::INPUT_PULLDOWN : arduino::INPUT_PULLUP);

    pinMode(DC_DC_3V3, arduino::OUTPUT);
    digitalWrite(DC_DC_3V3, DC_DC_ACTIF ? 1 : 0);         // DC-DC ON (I_But_Start OFF)
    pinMode(LED, arduino::OUTPUT);
    digitalWrite(LED, 0);

    CalCharger();

    Antirebond fOn  = {false, 0};
    Antirebond fFr  = {false, 0};
    Antirebond fCtl = {false, 0};
    Antirebond fSt  = {false, 0};

    // Précharge / DC-DC
    bool     startPrec    = false;
    uint32_t startDepuis  = 0;

    // Calibration
    bool     calActif     = false;
    bool     calAuto      = false;     // lancée par la commande secrète
    uint32_t calDebut     = 0;
    bool     frPrec       = false;     // commande secrète : front du F/R
    uint8_t  secretNb     = 0;
    uint32_t secretDebut  = 0;
    int      calMin       = 1023;
    int      calMax       = 0;
    uint8_t  calRes       = 0;
    uint32_t ledOkJusqua  = 0;
    uint32_t cycle        = 0;

    TickType_t lastWakeTime = xTaskGetTickCount();

    while (1)
    {
        uint32_t t_debut = micros();
        uint32_t now     = millis();

        // 1) Switchs : lecture brute + anti-rebond
        bool bOn  = (digitalRead(I_Switch_ON)   == SWITCH_ACTIF);
        bool bFr  = (digitalRead(I_F_R)         == SWITCH_ACTIF);
        bool bCtl = (digitalRead(Relay_Control) == SWITCH_ACTIF);
        bool bSt  = (digitalRead(I_But_Start)   == ESC_DEMARRES_ACTIF);

        PiloteData p = {};
        p.sw_brut      = (uint8_t)((bOn ? 1 : 0) | (bFr ? 2 : 0) | (bCtl ? 4 : 0) | (bSt ? 8 : 0));
        p.switch_on    = Filtrer(fOn,  bOn);
        p.switch_fr    = Filtrer(fFr,  bFr);
        p.switch_ctl   = Filtrer(fCtl, bCtl);
        p.esc_demarres = Filtrer(fSt,  bSt);

        // 2) DC-DC : ON tant que I_But_Start est OFF ; coupé quand
        //    I_But_Start est ON depuis PRECHARGE_MS ; remis à ON dès
        //    que I_But_Start retombe.
        if (p.esc_demarres) {
            if (!startPrec) startDepuis = now;
        }
        bool dcdc = !(p.esc_demarres && (now - startDepuis >= PRECHARGE_MS));
        startPrec = p.esc_demarres;
        digitalWrite(DC_DC_3V3, dcdc ? DC_DC_ACTIF : !DC_DC_ACTIF);
        p.dcdc = dcdc;

        // 3) Levier
        p.levier_raw = adc_lire(POT_Levier);

        // 4) Calibration du levier, pilotée par l'UI (EVT_CALIB_*).
        //    Autorisée en IDLE seulement (switch ON à OFF).
#if CALIB_LEVIER_ACTIVE
        EventBits_t demande = xEventGroupClearBits(egEtat, EVT_CALIB_MASK);   // bits AVANT effacement

        // Commande secrète : CALIB_SECRET_NB passages du F/R à actif en
        // moins de CALIB_SECRET_FENETRE_MS, en IDLE → calibration auto.
        bool secret = false;
        if (p.switch_on || calActif) {
            secretNb = 0;
        } else if (p.switch_fr && !frPrec) {                 // front montant (filtré)
            if (secretNb == 0 || (now - secretDebut > CALIB_SECRET_FENETRE_MS)) {
                secretNb    = 1;
                secretDebut = now;
            } else if (++secretNb >= CALIB_SECRET_NB) {
                secret   = true;
                secretNb = 0;
            }
        }
        frPrec = p.switch_fr;

        if (!calActif && ((demande & EVT_CALIB_DEBUT) || secret)) {
            if (p.switch_on) {
                calRes = 5;                          // refusée : pas en IDLE
            } else {
                calActif = true;
                calAuto  = secret;
                calDebut = now;
                calMin   = 1023;
                calMax   = 0;
                calRes   = 0;
            }
        }
        if (calActif) {
            if (p.levier_raw < calMin) calMin = p.levier_raw;
            if (p.levier_raw > calMax) calMax = p.levier_raw;

            if (p.switch_on || (demande & EVT_CALIB_ANNULE) ||
                (now - calDebut > CALIB_TIMEOUT_MS)) {
                calRes   = 4;                        // annulée
                calActif = false;
            }
            else if ((demande & EVT_CALIB_FIN) ||
                     (calAuto && (now - calDebut >= CALIB_AUTO_DUREE_MS))) {
                int nouvMax = calMax - CALIB_MARGE_MAX;
                if (calMax - calMin < CALIB_PLAGE_MIN || !CalValide(calMin, nouvMax)) {
                    calRes = 2;                      // course trop courte
                } else if (p.levier_raw > calMin + CALIB_RETOUR_REPOS) {
                    calRes = 3;                      // levier pas revenu au repos
                } else {
                    levMin = calMin;
                    levMax = nouvMax;
                    CalSauver(levMin, levMax);
                    calRes = 1;
                    ledOkJusqua = now + 2000;
                }
                calActif = false;
            }
        }
#endif
        // LED : clignote pendant la calibration, fixe 2 s si OK
        bool led = calActif ? (((cycle / 5) % 2) == 0) : ((int32_t)(ledOkJusqua - now) > 0);
        digitalWrite(LED, led ? 1 : 0);
        cycle++;

        p.calib   = calActif ? (calAuto ? 2 : 1) : 0;
        p.cal_res = calRes;
        p.lev_min = (int16_t)levMin;
        p.lev_max = (int16_t)levMax;
        p.cal_min = (int16_t)(calActif ? calMin : 0);
        p.cal_max = (int16_t)(calActif ? calMax : 0);

        if (calActif) {
            // Pendant la calibration : aucune consigne
            p.levier     = 0;
            p.target_fwd = 0;
            p.target_rev = 0;
            p.au_neutre  = false;
        } else {
            p.levier     = LireLevier(p.levier_raw);
            p.target_fwd = CreateTargetForward(p.levier);
            p.target_rev = CreateTargetReverse(p.levier);
            p.au_neutre  = (p.levier_raw <= levMin + (LEVIER_RAW_ARME - LEVIER_RAW_MIN));
        }

        // Envoi à la machine d'état. File pleine = FSM figée : on
        // ne bloque pas, le watchdog détecte et coupe la consigne.
        FsmEvent ev;
        ev.type   = EvType::PILOTE;
        ev.pilote = p;
        xQueueSend(qEvents, &ev, 0);

        float duree_us = (float)(micros() - t_debut);

        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            Pilote_data     = p;
            Pilote_temps_us = duree_us;
            xSemaphoreGive(dataMutex);
        }

        wd_vivant(T_PILOTE);
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_PILOTE_MS));
    }
}
