#include "task_foils_control.h"
#include "task_sonar.h"
#include "task_xsens.h"
#include "rtos_com.h"        // egEtat : EVT_CONTROLE (machine d'état)
#include "task_watchdog.h"

extern SemaphoreHandle_t dataMutex;

// Définitions des variables partagées
float H_outputs[NB_CANAUX]      = {0.0f, 0.0f, 0.0f};
float P_output                  =  0.0f;
float R_output                  =  0.0f;
float HPR_cmd_servos[NB_CANAUX] = {0.0f, 0.0f, 0.0f};
float HPR_control_time_us       =  0.0f;
float cmd_filt[NB_CANAUX]       = {SERVO_US_CENTRE, SERVO_US_CENTRE, SERVO_US_CENTRE};   // µs
float H_prev_dist[NB_CANAUX]    = {0.0f, 0.0f, 0.0f};
float distance_ref[NB_CANAUX]   = {SONAR_DISTANCE_REF_AVANT, SONAR_DISTANCE_REF_ARRIERE_GAUCHE, SONAR_DISTANCE_REF_ARRIERE_DROIT};
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

    // Pas d'intégration au-dessus de H_GRANDE_ERREUR (décollage) : sinon
    // l'intégrale se remplit pendant la montée et fait dépasser la consigne.
    if (absE < H_GRANDE_ERREUR) H_integral[i] += error * dt;
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

    // Servos : PWM initialisé par Servos_Init() (main.cpp). Le filtre part
    // du neutre calibré ; la 1re commande (ci-dessous) est le neutre.
    for (int i = 0; i < NB_CANAUX; i++) {
        float mn, neu, mx;
        if (Servo_Bornes(i, mn, neu, mx)) cmd_filt[i] = neu;
    }

    while (1)
    {
        uint32_t t_debut = micros();

        float dist[NB_CANAUX] = {};
        float pitch_deg = 0.0f;
        float roll_deg  = 0.0f;
        bool  xsens_ok  = false;
        float hauteur_out[NB_CANAUX];
        float pitch_out;
        float roll_out;
        float servo_raw[NB_CANAUX];     // µs
        float cmd[NB_CANAUX];           // µs
        float s_min[NB_CANAUX], s_neu[NB_CANAUX], s_max[NB_CANAUX];
        bool  s_ok[NB_CANAUX];
        for (int i = 0; i < NB_CANAUX; i++)
            s_ok[i] = Servo_Bornes(i, s_min[i], s_neu[i], s_max[i]);

        TickType_t now = xTaskGetTickCount();
        float dt = (now - prevTick) * portTICK_PERIOD_MS / 1000.0f;
        prevTick = now;
        if (dt < 0.0001f) dt = 0.0001f;

        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            for (int i = 0; i < NB_CANAUX; i++) {
                dist[i] = Sonar_distance[i];
                // Consigne = zéro sur l'eau + hauteur de vol (sans zéro : CONTROLE refusé)
                if (Sonar_zero_ok) distance_ref[i] = Sonar_zero[i] + H_VOL[i];
            }
            pitch_deg = Xsens_data.pitch;
            roll_deg  = Xsens_data.roll;
            xsens_ok  = Xsens_data.att_valid;
            xSemaphoreGive(dataMutex);
        }

        // Hauteur (FOILS_HAUTEUR_ACTIF à 0 : sortie et intégrale à 0)
        for (int i = 0; i < NB_CANAUX; i++) {
            if (FOILS_HAUTEUR_ACTIF) {
                hauteur_out[i] = pid_hauteur(i, dist[i], dt);
            } else {
                hauteur_out[i] = 0.0f;
                H_integral[i]  = 0.0f;
                H_prev_dist[i] = dist[i];
            }
        }

        // Pitch et Roll (FOILS_PITCH_ACTIF / FOILS_ROLL_ACTIF à 0 : idem)
        if (FOILS_PITCH_ACTIF && xsens_ok) {
            pitch_out = pid_pitch(pitch_deg, dt);
        } else {
            pitch_out    = 0.0f;
            P_integral   = 0.0f;
            P_prev_pitch = pitch_deg;
        }
        if (FOILS_ROLL_ACTIF && xsens_ok) {
            roll_out = pid_roll(roll_deg, dt);
        } else {
            roll_out    = 0.0f;
            R_integral  = 0.0f;
            R_prev_roll = roll_deg;
        }

        // Sorties des PID en degrés servo → µs autour du neutre calibré
        // SERVO_SENS_x : -1 pour un servo monté à l'envers (config.h)
        servo_raw[0] = s_neu[0] + SERVO_SENS_AVANT          * (hauteur_out[0] + pitch_out)            * SERVO_US_PAR_DEG;   // avant
        servo_raw[1] = s_neu[1] + SERVO_SENS_ARRIERE_GAUCHE * (hauteur_out[1] - pitch_out - roll_out) * SERVO_US_PAR_DEG;   // arr. gauche
        servo_raw[2] = s_neu[2] + SERVO_SENS_ARRIERE_DROIT  * (hauteur_out[2] - pitch_out + roll_out) * SERVO_US_PAR_DEG;   // arr. droit

        // AJOUT : PID appliqué uniquement en CONTROLE (machine d'état).
        // Sinon : neutre + intégrateurs à zéro (pas de windup, réentrée
        // propre sans pic de dérivée).
        bool foils_actifs = (xEventGroupGetBits(egEtat) & EVT_CONTROLE) != 0;
        if (!foils_actifs) {
            for (int i = 0; i < NB_CANAUX; i++) {
                servo_raw[i]   = s_neu[i];
                H_integral[i]  = 0.0f;
                H_prev_dist[i] = dist[i];
            }
            P_integral   = 0.0f;
            R_integral   = 0.0f;
            P_prev_pitch = pitch_deg;
            R_prev_roll  = roll_deg;
        }
        
        // Pendant une calibration (UI), Task_RPi commande les servos : on n'écrit rien.
        bool calib = ServoCalib_Active();
        for (int i = 0; i < NB_CANAUX; i++) {
            if (!s_ok[i]) {                 // pas de calibration valide : servo libre
                cmd[i] = 0.0f;
                if (!calib) Servo_Couper(i);
                continue;
            }
            cmd_filt[i] = SERVO_ALPHA * servo_raw[i] + (1.0f - SERVO_ALPHA) * cmd_filt[i];
            cmd_filt[i] = fclamp(cmd_filt[i], s_min[i], s_max[i]);
            cmd[i]      = cmd_filt[i];
            if (!calib) Servo_Ecrire(i, cmd[i]);
        }

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

        wd_vivant(T_FOILS);
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_height_control_MS));
    }
}