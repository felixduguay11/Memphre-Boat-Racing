// =====================================================
//  task_pilote.cpp
//  Tâche FreeRTOS — entrées du pilote, 50 Hz.
//
//  Portage de Real_V1 :
//    - levier : bornes mesurées, deadband, mapping eRPM
//      (LireLevier, CreateTargetForward, CreateTargetReverse)
//    - switchs en pull-up : LOW = actif (SWITCH_ACTIF)
//  Le volant (POT_Volant) sera ajouté ici plus tard.
// =====================================================

#include "task_pilote.h"
#include "task_watchdog.h"

// ─── Définition des variables partagées ───
PiloteData Pilote_data     = {false, false, false, false, 0, 0, 0, 0};
float      Pilote_temps_us = 0.0f;

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
    if (raw <= LEVIER_RAW_MIN) return 0;
    if (raw >= LEVIER_RAW_MAX) return 1023;
    return (int)map_long(raw, LEVIER_RAW_MIN, LEVIER_RAW_MAX, 0, 1023);
}

// Sous ERPM_MIN_UTILE le moteur ne tourne pas proprement : dès que
// le levier sort du deadband on saute à ERPM_MIN_UTILE, puis linéaire.
static int CreateTargetForward(int lev)
{
    if (lev == 0) return 0;                 // repos : moteur arrêté
    return (int)map_long(lev, 1, 1023, ERPM_MIN_UTILE, ERPM_MAX_FORWARD);
}

static int CreateTargetReverse(int lev)
{
    if (lev == 0) return 0;                 // repos : moteur arrêté
    return -(int)map_long(lev, 1, 1023, ERPM_MIN_UTILE, ERPM_MAX_REVERSE);
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
    pinMode(I_But_Start,   arduino::INPUT_PULLUP);

    TickType_t lastWakeTime = xTaskGetTickCount();

    while (1)
    {
        uint32_t t_debut = micros();

        PiloteData p;
        p.switch_on    = (digitalRead(I_Switch_ON)   == SWITCH_ACTIF);
        p.switch_fr    = (digitalRead(I_F_R)         == SWITCH_ACTIF);
        p.switch_ctl   = (digitalRead(Relay_Control) == SWITCH_ACTIF);
        p.esc_demarres = (digitalRead(I_But_Start)   == ESC_DEMARRES_ACTIF);

        p.levier_raw   = adc_lire(POT_Levier);
        p.levier       = LireLevier(p.levier_raw);
        p.target_fwd   = CreateTargetForward(p.levier);
        p.target_rev   = CreateTargetReverse(p.levier);

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
