// =====================================================
//  task_state_machine.cpp
//  Machine d'état + pilotage moteurs/direction par RC
//  Portage du miniature (loop) → tâche FreeRTOS
// =====================================================

#include "task_state_machine.h"
#include <PWMServo.h>
#include "Receiver.h"
#include "ESC.h"
#include "StateMachine.h"

extern SemaphoreHandle_t dataMutex;

// ─── Objets pilotes possédés par CETTE tâche uniquement ───
static RCReceiver   rc;
static ESC          esc;
static StateMachine sm;

// Servo de direction (rudder) — inline pour éviter d'importer
// ServoFoils (les foils sont déjà gérés par Task_foils_Control)
static PWMServo     rudderServo;

// ─── Définition des variables partagées ───
TopState SM_top_state           = TopState::IDLE;
RunState SM_run_state           = RunState::NEUTRE;
bool     SM_foil_control_active = false;
float    SM_rc_throttle         = 0.0f;
float    SM_rc_rudder           = 0.0f;
bool     SM_rc_valid            = false;
float    SM_control_time_us     = 0.0f;

// ─── Rudder : helpers (mêmes mappings que ServoRudder du miniature) ─
//  cmd [-1,+1] → 1000..2000 µs ;  500µs→0°, 1500µs→90°, 2500µs→180°
static uint8_t rudderUsToDeg(uint16_t us)
{
    float deg = (float)(us - 500) / (2500.0f - 500.0f) * 180.0f;
    if (deg > 180.0f) deg = 180.0f;
    if (deg <   0.0f) deg =   0.0f;
    return (uint8_t)deg;
}

static void rudderSet(float cmd)
{
    if (cmd >  1.0f) cmd =  1.0f;
    if (cmd < -1.0f) cmd = -1.0f;
    float pulse = RUDDER_CENTER_US + cmd * (float)(RUDDER_PULSE_MAX_US - RUDDER_CENTER_US);
    if (pulse > RUDDER_PULSE_MAX_US) pulse = RUDDER_PULSE_MAX_US;
    if (pulse < RUDDER_PULSE_MIN_US) pulse = RUDDER_PULSE_MIN_US;
    rudderServo.write(rudderUsToDeg((uint16_t)pulse));
}

static void rudderCenter()
{
    rudderServo.write(rudderUsToDeg(RUDDER_CENTER_US));
}

// =====================================================
//  Task_StateMachine — 50 Hz
// =====================================================
void Task_StateMachine(void *ptr)
{
    (void) ptr;

    rc.begin();                       // attache les interruptions RC
    esc.begin();                      // ARME les 2 ESC (bloque ESC_ARM_DELAY_MS)
    rudderServo.attach(PIN_SERVO_RUDDER);
    rudderCenter();
    sm.begin();

    TickType_t lastWakeTime = xTaskGetTickCount();

    while (1)
    {
        uint32_t t_debut = micros();

        // 1) Lecture manette (consomme les captures ISR, normalise)
        rc.update();

        SMInputs in;
        in.throttle = rc.throttle();   // [0, +1]
        in.rudder   = rc.rudder();     // [-1, +1]
        in.switchA  = rc.switchA();    // armement RUN
        in.switchB  = false;           // inutilisé
        in.switchC  = rc.switchC();    // marche arrière
        in.rcValid  = rc.isValid();

        // 2) Mise à jour machine d'état
        sm.update(in);

        TopState top = sm.getTopState();
        RunState run = sm.getRunState();
        bool foils_actifs = (top == TopState::RUN && run == RunState::CONTROLE);

        // 3) Pilotage moteurs + direction selon l'état
        switch (top)
        {
            case TopState::IDLE:
            case TopState::STOP:
                esc.stop();
                rudderCenter();
                break;

            case TopState::RUN:
                rudderSet(in.rudder);
                switch (run)
                {
                    case RunState::NEUTRE:   esc.stop();             break;
                    case RunState::AVANCE:   esc.set(in.throttle);   break;
                    case RunState::RECULE:   esc.set(-in.throttle);  break;
                    case RunState::CONTROLE: esc.set(in.throttle);   break;
                }
                break;
        }

        float duree_us = (float)(micros() - t_debut);

        // 4) Publication de l'état (lu par Task_foils_Control + Task_RPi)
        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            SM_top_state           = top;
            SM_run_state           = run;
            SM_foil_control_active = foils_actifs;
            SM_rc_throttle         = in.throttle;
            SM_rc_rudder           = in.rudder;
            SM_rc_valid            = in.rcValid;
            SM_control_time_us     = duree_us;
            xSemaphoreGive(dataMutex);
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_SM_MS));
    }
}
