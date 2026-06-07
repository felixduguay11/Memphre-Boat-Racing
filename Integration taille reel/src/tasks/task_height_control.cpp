#include "task_height_control.h"
#include "task_sonar.h"

extern SemaphoreHandle_t dataMutex;
static PWMServo servo_avant;

// Fonction interne : valeur absolue float
static inline float fabs_local(float v) {
  return (v < 0.0f) ? -v : v;
}

// Fonction interne : constrain float
static inline float fclamp(float v, float lo, float hi) {
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

// Fuzzy-PID
static float calcul_pid(int i, float dist, float dt)
{
  float error = distance_ref[i] - dist;
  float absE  = fabs_local(error);
  if (absE < H_DEADBAND_ERR) error = 0.0f;
  integral_pid[i] += error * dt;
  integral_pid[i]  = fclamp(integral_pid[i], H_INTEGRAL_MAX, H_INTEGRAL_MIN);
  float delta     = dist - prev_target[i];
  float raw_deriv = (fabs_local(delta) > H_DEADBAND_DERIV) ? (-delta / dt) : 0.0f;
  deriv_filt_pid[i] = H_ALPHA_DERIV * raw_deriv + (1.0f - H_ALPHA_DERIV) * deriv_filt_pid[i];
  prev_target[i] = dist;

  // Gains Fuzzy
  if (absE > 10.0f) {
    return H_KP_HAUT * error + H_KI_HAUT * integral_pid[i] + H_KD_HAUT * deriv_filt_pid[i];
  } else if (absE > 5.0f) {
    return H_KP_MID * error + H_KI_MID * integral_pid[i] + H_KD_MID * deriv_filt_pid[i];
  } else {
    return H_KP_BAS * error + H_KI_BAS * integral_pid[i] + H_KD_BAS * deriv_filt_pid[i];
  }
}

// Height Control
void Task_Height_Control(void *ptr)
{
  (void) ptr;
  TickType_t lastWakeTime = xTaskGetTickCount();
  TickType_t prevTick     = lastWakeTime;

  servo_avant.attach(PWM_Servo_Avant);
  servo_avant.write(SERVO_NEUTRAL[0]);

  while (1)
  {
    uint32_t t_debut = micros();

    float dist[NB_CANAUX];
    float out[NB_CANAUX];
    float cmd[NB_CANAUX];
    float servo[NB_CANAUX];

    TickType_t now = xTaskGetTickCount();
    float dt = (now - prevTick) * portTICK_PERIOD_MS / 1000.0f;
    prevTick = now;
    if (dt < 0.0001f) dt = 0.0001f;

    for(int i=0; i<NB_CANAUX; i++){
      dist[i] = lire_sonar(PINS_SONARS[i], distance_filt[i]);
      distance_filt[i] = dist[i];
    }
    
    for(int i=0; i<NB_CANAUX; i++)
      out[i] = calcul_pid(i, dist[i], dt);

    for(int i=0; i<NB_CANAUX; i++){
      servo[i] = SERVO_NEUTRAL[i] + out[i];
      cmd_filt[i] = SERVO_ALPHA * servo[i] + (1.0f - SERVO_ALPHA) * cmd_filt[i];
      cmd[i] = fclamp(cmd_filt[i], SERVO_MIN[i], SERVO_MAX[i]);
    }

    servo_avant.write((int)cmd[0]);

    float duree_us = (float)(micros() - t_debut);

    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      for(int i=0; i<NB_CANAUX; i++){
        H_outputs[i]      = out[i];
        H_cmd_servos[i]   = cmd[i];
        Sonar_distance[i] = dist[i];
      } 
      H_control_time_us = duree_us;
      xSemaphoreGive(dataMutex);
    }

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_height_control_MS));
  }
}
