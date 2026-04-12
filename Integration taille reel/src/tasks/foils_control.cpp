#include "foils_control.h"
#include "task_sonar.h"

// =====================================================
// Définition des variables partagées
// déclarées extern dans foils_control.h
// =====================================================
float g_output_pid        = 0.0f;
float g_cmd_servo         = 0.0f;
float g_temps_controle_us = 0.0f;

// Mutex défini dans main.cpp
extern SemaphoreHandle_t dataMutex;

// =====================================================
// Variables internes PID — propres à cette tâche
// =====================================================
static float s_prev_target    = DISTANCE_REF_CM;
static float s_integral_pid   = 0.0f;
static float s_deriv_filt_pid = 0.0f;
static float s_cmd_filt       = (float)SERVO_NEUTRAL;

// Filtre sonar propre à cette tâche
static float s_distance_filt_ctrl = DISTANCE_REF_CM;

// Servo — instance locale
static PWMServo s_servo;

// =====================================================
// Utilitaire interne : valeur absolue float
// =====================================================
static inline float fabs_local(float v) {
  return (v < 0.0f) ? -v : v;
}

// =====================================================
// Utilitaire interne : constrain float
// =====================================================
static inline float fclamp(float v, float lo, float hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

// =====================================================
// calcul_pid()
//
// PID Fuzzy — gains adaptatifs selon l'amplitude de l'erreur
//   |erreur| > 10cm  → gains agressifs (rattrapage rapide)
//   |erreur| > 5cm   → gains moyens
//   |erreur| <= 5cm  → gains doux (stabilisation fine)
//
// Dérivée filtrée avec zone morte pour ignorer le bruit
// résiduel du capteur (~0.3cm)
// =====================================================
static float calcul_pid(float dist, float dt)
{
  float error = DISTANCE_REF_CM - dist;
  float absE  = fabs_local(error);

  // Zone morte erreur — évite les oscillations autour de la consigne
  if (absE < ZONE_MORTE_ERR) error = 0.0f;

  // Intégrale — antiwindup par saturation
  s_integral_pid += error * dt;
  s_integral_pid  = fclamp(s_integral_pid, -20.0f, 20.0f);

  // Dérivée filtrée avec zone morte
  float delta     = dist - s_prev_target;
  float raw_deriv = (fabs_local(delta) > ZONE_MORTE_DERIV) ? (-delta / dt) : 0.0f;
  s_deriv_filt_pid = ALPHA_DERIVEE * raw_deriv
                   + (1.0f - ALPHA_DERIVEE) * s_deriv_filt_pid;
  s_prev_target = dist;

  // Gains Fuzzy
  float Kp, Ki, Kd;
  if (absE > 10.0f) {
    Kp = 1.6f;  Ki = 0.0f;   Kd = 1.9f;  // rattrapage rapide
  } else if (absE > 5.0f) {
    Kp = 0.6f;  Ki = 0.05f;  Kd = 0.4f;  // approche
  } else {
    Kp = 0.4f;  Ki = 0.1f;   Kd = 0.2f;  // stabilisation fine
  }

  return Kp * error + Ki * s_integral_pid + Kd * s_deriv_filt_pid;
}


// =====================================================
// Task_FoilsControl — priorité 3 (haute), période 20ms
//
// Rôle :
//   1. Lit la distance via lire_sonar() (filtre propre)
//   2. Calcule la commande PID Fuzzy
//   3. Applique le filtre servo + contrainte physique
//   4. Envoie la commande au servo
//   5. Met à jour les variables partagées pour le debug
// =====================================================
void Task_FoilsControl(void *ptr)
{
  (void) ptr;
  TickType_t lastWakeTime = xTaskGetTickCount();
  TickType_t prevTick     = lastWakeTime;

  // Attache le servo au bon pin
  s_servo.attach(PIN_SERVO);
  s_servo.write(SERVO_MIN);

  while (1)
  {
    uint32_t t_debut = micros();

    // --- dt en secondes ---
    TickType_t now = xTaskGetTickCount();
    float dt = (now - prevTick) * portTICK_PERIOD_MS / 1000.0f;
    prevTick = now;
    if (dt < 0.0001f) dt = 0.0001f;

    // --- Lecture sonar avec filtre propre à cette tâche ---
    float dist = lire_sonar(PIN_SONAR, s_distance_filt_ctrl);

    // --- PID Fuzzy ---
    float out = calcul_pid(dist, dt);

    // --- Filtre servo + contrainte physique ---
    float servo_raw = (float)SERVO_NEUTRAL + out;
    s_cmd_filt      = ALPHA_SERVO * servo_raw + (1.0f - ALPHA_SERVO) * s_cmd_filt;
    float cmd       = fclamp(s_cmd_filt, (float)SERVO_MIN, (float)SERVO_MAX);

    // --- Envoi commande servo ---
    s_servo.write((int)cmd);

    float duree_us = (float)(micros() - t_debut);

    // --- Mise à jour variables partagées ---
    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      g_output_pid        = out;
      g_cmd_servo         = cmd;
      g_distance          = dist;   // met à jour g_distance pour le debug
      g_temps_controle_us = duree_us;
      xSemaphoreGive(dataMutex);
    }

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_CTRL_MS));
  }
}
