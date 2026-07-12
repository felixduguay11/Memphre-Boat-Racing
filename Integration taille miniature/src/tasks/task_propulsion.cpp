// =====================================================
//  task_propulsion.cpp
//  Tâche FreeRTOS — propulsion (FlySky + 2 ESC + rudder), 50 Hz.
//
//  Portage du miniature (loop) → tâche FreeRTOS.
//  Le rudder est géré INLINE (PWMServo direct) plutôt que via la
//  classe ServoRudder du miniature, pour éviter d'importer ServoFoils
//  (les foils appartiennent déjà à Task_foils_Control).
//  Mappings du rudder identiques à ServoRudder du miniature.
// =====================================================

#include "task_propulsion.h"
#include <PWMServo.h>
#include "Receiver.h"
#include "ESC.h"

extern SemaphoreHandle_t dataMutex;

// ─── Objets pilotes possédés EN EXCLUSIVITÉ par cette tâche ───
static RCReceiver rc;
static ESC        esc;
static PWMServo   rudderServo;

// ─── Définition des variables partagées (déclarées extern dans le .h) ───
float Prop_rc_throttle     = 0.0f;
float Prop_rc_rudder       = 0.0f;
bool  Prop_rc_switchA      = false;
bool  Prop_rc_switchC      = false;
bool  Prop_rc_valid        = false;
float Prop_control_time_us = 0.0f;

// ─── Rudder inline (mêmes mappings que ServoRudder du miniature) ───
//  cmd [-1,+1] → 1000..2000 µs ; puis 500µs→0°, 1500µs→90°, 2500µs→180°
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
//  Task_Propulsion — 50 Hz
// =====================================================
void Task_Propulsion(void *ptr)
{
    (void) ptr;

    rc.begin();                          // attache les interruptions RC
    esc.begin();                         // ARME les 2 ESC (attente vTaskDelay ~2 s)
    rudderServo.attach(PIN_SERVO_RUDDER);
    rudderCenter();

    TickType_t lastWakeTime = xTaskGetTickCount();

    while (1)
    {
        uint32_t t_debut = micros();

        // 1) Lecture manette (consomme les captures ISR, normalise).
        rc.update();
        float throttle = rc.throttle();   // [0, +1]
        float rudder   = rc.rudder();     // [-1, +1]
        bool  swA      = rc.switchA();    // armement RUN
        bool  swC      = rc.switchC();    // marche arrière
        bool  rcValid  = rc.isValid();

        // 2+3) Publier le snapshot manette ET lire l'état SM (même section critique).
        TopState top = TopState::STOP;    // défaut sûr si mutex indisponible
        RunState run = RunState::NEUTRE;
        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            // publier (pour Task_StateMachine + RPi)
            Prop_rc_throttle = throttle;
            Prop_rc_rudder   = rudder;
            Prop_rc_switchA  = swA;
            Prop_rc_switchC  = swC;
            Prop_rc_valid    = rcValid;
            // lire la décision d'état (publiée par Task_StateMachine)
            top = SM_top_state;
            run = SM_run_state;
            xSemaphoreGive(dataMutex);
        }

        // 4) Application aux actionneurs.
        //    FAILSAFE MATÉRIEL : perte manette => arrêt immédiat,
        //    indépendant de la latence de la machine d'état.
        if (!rcValid)
        {
            esc.stop();
            rudderCenter();
        }
        else
        {
            switch (top)
            {
                case TopState::IDLE:
                case TopState::STOP:
                    esc.stop();
                    rudderCenter();
                    break;

                case TopState::RUN:
                    rudderSet(rudder);
                    switch (run)
                    {
                        case RunState::NEUTRE:   esc.stop();           break;
                        case RunState::AVANCE:   esc.set(throttle);    break;
                        case RunState::RECULE:   esc.set(-throttle);   break;
                        case RunState::CONTROLE: esc.set(throttle);    break;
                    }
                    break;
            }
        }

        float duree_us = (float)(micros() - t_debut);

        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            Prop_control_time_us = duree_us;
            xSemaphoreGive(dataMutex);
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_PROP_MS));
    }
}
