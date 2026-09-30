// =====================================================
//  task_esc_data.cpp
//  Tâche FreeRTOS — données VESC (CAN3), 200 Hz.
//  Bus, IDs et décodage repris tels quels de Real_V1.
// =====================================================

// ⚠ En PREMIER : FlexCAN_T4 a besoin des macros Arduino
//   que arduino_freertos.h retire ensuite.
#include <FlexCAN_T4.h>
#include "VescCAN_Teensy.h"

#include "task_esc_data.h"
#include "task_watchdog.h"

// CAN3 sur Teensy 4.1 = pin 30 (CRX3) / pin 31 (CTX3)
static FlexCAN_T4<CAN3, RX_SIZE_256, TX_SIZE_16> rawCan1;
static FlexCANAdapter<CAN3> adapterCan1(rawCan1);
static VescCANBus           vesc(adapterCan1);

// Accès au périphérique CAN : lecture (cette tâche) + écriture (Propulsion)
static SemaphoreHandle_t canMutex = nullptr;

static const uint8_t VESC_IDS[NB_VESC] = {VESC_ID_A, VESC_ID_B};

// ─── Définition des variables partagées ───
EscData ESC_data[NB_VESC] = {};
float   ESC_temps_us      = 0.0f;

void ESC_InitCAN()
{
    canMutex = xSemaphoreCreateMutex();
    rawCan1.begin();
    rawCan1.setBaudRate(BAUD_CAN);

    // Par défaut FlexCAN n'a que 4 mailboxes RX pour les trames
    // étendues (celles des VESC). Real_V1 vidait le bus en continu ;
    // ici la tâche passe toutes les 5 ms → 32 mailboxes : 0-15 RX
    // étendues, 16-31 TX. À 250 kbps le bus porte ~1,9 trame/ms au
    // max : 16 mailboxes = ~8 ms de marge avant perte.
    rawCan1.setMaxMB(32);
    for (int i = 0; i < 16; i++) {
        rawCan1.setMB((FLEXCAN_MAILBOX)i, RX, EXT);
    }

    vesc.begin();
}

void ESC_EnvoyerERPM(int32_t erpm_a, int32_t erpm_b)
{
    if (xSemaphoreTake(canMutex, portMAX_DELAY))
    {
        vesc.setERPM(VESC_ID_A, erpm_a);
        vesc.setERPM(VESC_ID_B, erpm_b);
        xSemaphoreGive(canMutex);
    }
}

// Real_V1 : vescVivant()
static bool vescVivant(uint8_t id)
{
    return vesc.isUpdated(id) &&
           (millis() - vesc.lastUpdateMs(id) < VESC_TIMEOUT_MS);
}

// =====================================================
//  Task_ESC_Data — 200 Hz
// =====================================================
void Task_ESC_Data(void *ptr)
{
    (void) ptr;
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (1)
    {
        uint32_t t_debut = micros();

        // Lecture CAN — toujours, sinon le buffer déborde
        if (xSemaphoreTake(canMutex, portMAX_DELAY))
        {
            vesc.update();
            xSemaphoreGive(canMutex);
        }

        // Copie locale (vesc._data n'est écrit que par cette tâche)
        EscData d[NB_VESC];
        for (int k = 0; k < NB_VESC; k++)
        {
            uint8_t id  = VESC_IDS[k];
            d[k].id      = id;
            d[k].vivant  = vescVivant(id);
            d[k].deja_vu = vesc.isUpdated(id);
            d[k].erpm    = vesc.getERPM(id);
            d[k].duty    = vesc.getDutyCycle(id);
            d[k].i_mot   = vesc.getMotorCurrent(id);
            d[k].i_in    = vesc.getCurrentIn(id);
            d[k].v_in    = vesc.getVoltageIn(id);
            d[k].t_fet   = vesc.getTempFET(id);
            d[k].t_mot   = vesc.getTempMotor(id);
        }

        float duree_us = (float)(micros() - t_debut);

        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            for (int k = 0; k < NB_VESC; k++) ESC_data[k] = d[k];
            ESC_temps_us = duree_us;
            xSemaphoreGive(dataMutex);
        }

        wd_vivant(T_ESC);
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_ESC_MS));
    }
}
