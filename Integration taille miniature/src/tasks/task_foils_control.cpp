#include "task_foils_control.h"
#include "task_sonar.h"
#include "task_xsens.h"

extern SemaphoreHandle_t dataMutex;

static PWMServo servo_avant;
static PWMServo servo_arriere_gauche;
static PWMServo servo_arriere_droit;

// Définitions des variables partagées
float H_outputs[NB_CANAUX]      = {0.0f, 0.0f, 0.0f};
float P_output                  =  0.0f;
float R_output                  =  0.0f;
float HPR_cmd_servos[NB_CANAUX] = {0.0f, 0.0f, 0.0f};
float HPR_control_time_us       =  0.0f;
float cmd_filt[NB_CANAUX]       = {SERVO_AVANT_NEUTRAL, SERVO_ARRIERE_GAUCHE_NEUTRAL, SERVO_ARRIERE_DROIT_NEUTRAL};
float H_prev_dist[NB_CANAUX]    = {0.0f, 0.0f, 0.0f};
float H_integral[NB_CANAUX]     = {0.0f, 0.0f, 0.0f};
float H_deriv[NB_CANAUX]        = {0.0f, 0.0f, 0.0f};
float P_prev_pitch              = 0.0f;
float P_integral                = 0.0f;
float P_deriv                   = 0.0f;
float R_prev_roll               = 0.0f;
float R_integral                = 0.0f;
float R_deriv                   = 0.0f;

// Utilitaires
static inline float fabs_local(float v) {
    return (v < 0.0f) ? -v : v;
}

static inline float fclamp(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

// PID Fuzzy Hauteur — par foil
static float pid_hauteur(int i, float dist, float dt)
{
    float error = distance_ref[i] - dist;
    float absE  = fabs_local(error);
    if (absE < H_DEADBAND_ERR) error = 0.0f;

    H_integral[i] += error * dt;
    H_integral[i]  = fclamp(H_integral[i], H_INTEGRAL_MIN, H_INTEGRAL_MAX);

    float delta = dist - H_prev_dist[i];
    H_deriv[i] = -delta / dt;
    if (fabs_local(delta) < H_DEADBAND_DERIV) H_deriv[i] = 0.0f;
    H_prev_dist[i] = dist;

    if (absE > H_GRANDE_ERREUR) {
        return H_KP_HAUT * error + H_KI_HAUT * H_integral[i] + H_KD_HAUT * H_deriv[i];
    } else if (absE > H_MOYENNE_ERREUR) {
        return H_KP_MID  * error + H_KI_MID  * H_integral[i] + H_KD_MID  * H_deriv[i];
    } else {
        return H_KP_BAS  * error + H_KI_BAS  * H_integral[i] + H_KD_BAS  * H_deriv[i];
    }
}

// PID Fuzzy pitch
static float pid_pitch(float pitch_deg, float dt)
{
    float error = P_REF_DEG - pitch_deg;
    float absE  = fabs_local(error);
    if (absE < P_DEADBAND_ERR) error = 0.0f;

    P_integral += error * dt;
    P_integral  = fclamp(P_integral, P_INTEGRAL_MIN, P_INTEGRAL_MAX);

    float delta = pitch_deg - P_prev_pitch;
    P_deriv = -delta / dt;
    if (fabs_local(delta) < P_DEADBAND_DERIV) P_deriv = 0.0f;
    P_prev_pitch = pitch_deg;

    if (absE > P_GRANDE_ERREUR) {
        return P_KP_HAUT * error + P_KI_HAUT * P_integral + P_KD_HAUT * P_deriv;
    } else if (absE > P_MOYENNE_ERREUR) {
        return P_KP_MID  * error + P_KI_MID  * P_integral + P_KD_MID  * P_deriv;
    } else {
        return P_KP_BAS  * error + P_KI_BAS  * P_integral + P_KD_BAS  * P_deriv;
    }
}

// PID Fuzzy Roll
static float pid_roll(float roll_deg, float dt)
{
    float error = R_REF_DEG - roll_deg;
    float absE  = fabs_local(error);
    if (absE < R_DEADBAND_ERR) error = 0.0f;

    R_integral += error * dt;
    R_integral  = fclamp(R_integral, R_INTEGRAL_MIN, R_INTEGRAL_MAX);

    float delta = roll_deg - R_prev_roll;
    R_deriv = -delta / dt;
    if (fabs_local(delta) < R_DEADBAND_DERIV) R_deriv = 0.0f;
    R_prev_roll = roll_deg;

    if (absE > R_GRANDE_ERREUR) {
        return R_KP_HAUT * error + R_KI_HAUT * R_integral + R_KD_HAUT * R_deriv;
    } else if (absE > R_MOYENNE_ERREUR) {
        return R_KP_MID  * error + R_KI_MID  * R_integral + R_KD_MID  * R_deriv;
    } else {
        return R_KP_BAS  * error + R_KI_BAS  * R_integral + R_KD_BAS  * R_deriv;
    }
}

void Task_foils_Control(void *ptr)
{
    (void) ptr;
    TickType_t lastWakeTime = xTaskGetTickCount();
    TickType_t prevTick     = lastWakeTime;

    servo_avant.attach(PWM_Servo_Avant);
    servo_arriere_gauche.attach(PWM_Servo_Arriere_Gauche);
    servo_arriere_droit.attach(PWM_Servo_Arriere_Droit);

    servo_avant.write(SERVO_AVANT_NEUTRAL);
    servo_arriere_gauche.write(SERVO_ARRIERE_GAUCHE_NEUTRAL);
    servo_arriere_droit.write(SERVO_ARRIERE_DROIT_NEUTRAL);

    while (1)
    {
        uint32_t t_debut = micros();

        float dist[NB_CANAUX];
        float pitch_deg = 0.0f;
        float roll_deg  = 0.0f;
        bool  xsens_ok  = false;
        float hauteur_out[NB_CANAUX];
        float pitch_out;
        float roll_out;
        float servo_raw[NB_CANAUX];
        float cmd[NB_CANAUX];

        TickType_t now = xTaskGetTickCount();
        float dt = (now - prevTick) * portTICK_PERIOD_MS / 1000.0f;
        prevTick = now;
        if (dt < 0.0001f) dt = 0.0001f;

        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            for (int i = 0; i < NB_CANAUX; i++) {
                dist[i] = Sonar_distance[i];
            }
            pitch_deg = Xsens_data.pitch;
            roll_deg  = Xsens_data.roll;
            xsens_ok  = Xsens_data.att_valid;
            xSemaphoreGive(dataMutex);
        }

        // Hauteur 
        for (int i = 0; i < NB_CANAUX; i++) {
            hauteur_out[i] = pid_hauteur(i, dist[i], dt);
        }

        // Pitch et Roll
        if(xsens_ok == true){
            pitch_out = pid_pitch(pitch_deg, dt);
            roll_out = pid_roll (roll_deg,  dt);
        }
        else {
            pitch_out = 0.0f;
            roll_out = 0.0f;
        }

        servo_raw[0] = (float)SERVO_NEUTRAL[0] + hauteur_out[0] + pitch_out;              // avant
        servo_raw[1] = (float)SERVO_NEUTRAL[1] + hauteur_out[1] - pitch_out - roll_out;   // arr. gauche
        servo_raw[2] = (float)SERVO_NEUTRAL[2] + hauteur_out[2] - pitch_out + roll_out;   // arr. droit
        
        for (int i = 0; i < NB_CANAUX; i++) {
            cmd_filt[i] = SERVO_ALPHA * servo_raw[i] + (1.0f - SERVO_ALPHA) * cmd_filt[i];
            cmd_filt[i] = fclamp(cmd_filt[i], (float)SERVO_MIN[i], (float)SERVO_MAX[i]);
            cmd[i]      = cmd_filt[i];
        }

        servo_avant.write((int)cmd[0]);
        servo_arriere_gauche.write((int)cmd[1]);
        servo_arriere_droit.write((int)cmd[2]);

        float duree_us = (float)(micros() - t_debut);

        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            for (int i = 0; i < NB_CANAUX; i++) {
                H_outputs[i]      = hauteur_out[i];
                HPR_cmd_servos[i] = cmd[i];
            }
            P_output             = pitch_out;
            R_output             = roll_out;
            HPR_control_time_us  = duree_us;
            xSemaphoreGive(dataMutex);
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_height_control_MS));
    }
}