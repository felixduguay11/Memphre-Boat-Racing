// =====================================================
//  task_rpi.cpp
//  Tâche FreeRTOS — télémétrie JSON vers le Raspberry Pi.
//
//  Format de Real_V1 conservé (clés t, mode, run, target, ramped,
//  hot, esc[]) → l'UI (Real_V1_UI) et l'outil d'analyse marchent
//  sans modification. Champs ajoutés (objets imbriqués, aplatis
//  par json_graph_ui.py en « imu.roll », « wd.pil.ok », ...) :
//    arme, pil{}, imu{}, gps{}, son{}, sio{}, foils{}, wd{}
//
//  Les données du watchdog sont dans LA MÊME trame : une trame
//  séparée ferait afficher « absent de la trame » aux cartes ESC.
// =====================================================

#include "task_rpi.h"
#include <cmath>
#include <cstdarg>
#include "task_pilote.h"
#include "task_state_machine.h"
#include "task_propulsion.h"
#include "task_esc_data.h"
#include "task_watchdog.h"

// Mutex défini dans main.cpp
extern SemaphoreHandle_t dataMutex;

// ─── Liaison Pi (Real_V1) ───
static bool     streaming   = false;
static uint32_t lastBeatMs  = 0;
static uint32_t lastPrintMs = 0;
static char     piLine[PI_LINE_MAX];
static uint8_t  piIdx       = 0;
static float    rpi_temps_us = 0.0f;

static char     json[RPI_JSON_MAX];
static int      jlen = 0;

static const char* NOMS_TACHES[NB_TACHES] = {
    "pil", "fsm", "prop", "esc", "xsens", "sonar", "foils", "rpi", "wd"
};

// ─── Chaînes d'état ───
static const char* modeStr(ControlMode m)
{
    switch (m) {
        case ControlMode::RUN:   return "RUN";
        case ControlMode::ARRET: return "ARRET";
        default:                 return "IDLE";
    }
}

static const char* runStr(RunMode r)
{
    switch (r) {
        case RunMode::FORWARD:  return "FORWARD";
        case RunMode::REVERSE:  return "REVERSE";
        case RunMode::CONTROLE: return "CONTROLE";
        default:                return "NEUTRAL";
    }
}

// NaN / inf casseraient le JSON (le Pi jetterait la trame)
static inline double fin(double v) { return std::isfinite(v) ? v : 0.0; }

// Ajout formaté au buffer JSON (tronqué proprement si plein)
static void ap(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void ap(const char* fmt, ...)
{
    if (jlen >= RPI_JSON_MAX - 1) return;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(json + jlen, RPI_JSON_MAX - jlen, fmt, args);
    va_end(args);
    if (n > 0) jlen += n;
    if (jlen > RPI_JSON_MAX - 1) jlen = RPI_JSON_MAX - 1;
}

// ─── Copie locale de Shared Data ───
struct Snapshot {
    ControlMode mode;  RunMode run;  bool arme;
    int   target;  float ramped;
    PiloteData pil;
    EscData    esc[NB_VESC];
    XsensData  xs;
    float dist[NB_CANAUX];  bool io[NB_CANAUX];
    float cmd[NB_CANAUX];   float h[NB_CANAUX];  float p_out;  float r_out;
    WdTache wd[NB_TACHES];  uint32_t err;  bool hot;  bool rst;
    float us[NB_TACHES];
};

static void lireSnapshot(Snapshot &s)
{
    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
        s.mode   = SM_mode;       s.run = SM_run;   s.arme = SM_arme;
        s.target = Prop_target;   s.ramped = Prop_ramped;
        s.pil    = Pilote_data;
        for (int k = 0; k < NB_VESC; k++) s.esc[k] = ESC_data[k];
        s.xs     = Xsens_data;
        for (int i = 0; i < NB_CANAUX; i++) {
            s.dist[i] = Sonar_distance[i];
            s.io[i]   = Sonar_io[i];
            s.cmd[i]  = HPR_cmd_servos[i];
            s.h[i]    = H_outputs[i];
        }
        s.p_out = P_output;
        s.r_out = R_output;
        for (int i = 0; i < NB_TACHES; i++) s.wd[i] = WD_taches[i];
        s.err = WD_erreurs;  s.hot = WD_surchauffe;  s.rst = WD_reset_wdt;

        s.us[T_PILOTE] = Pilote_temps_us;
        s.us[T_FSM]    = SM_temps_us;
        s.us[T_PROP]   = Prop_temps_us;
        s.us[T_ESC]    = ESC_temps_us;
        s.us[T_XSENS]  = Xsens_temps_us;
        s.us[T_SONAR]  = Sonar_temps_us;
        s.us[T_FOILS]  = HPR_control_time_us;
        s.us[T_RPI]    = rpi_temps_us;
        s.us[T_WD]     = WD_temps_us;
        xSemaphoreGive(dataMutex);
    }
}

// ─────────────────────────────────────────────────────────────
//  Émission de la télémétrie — format contractuel avec l'UI
// ─────────────────────────────────────────────────────────────
static void sendTelemetry()
{
    Snapshot s = {};
    lireSnapshot(s);

    jlen = 0;
    // --- Clés de Real_V1 ---
    ap("{\"t\":%lu,\"mode\":\"%s\",\"run\":\"%s\",\"target\":%d,\"ramped\":%ld,\"hot\":%d,\"esc\":[",
       (unsigned long)millis(), modeStr(s.mode), runStr(s.run),
       s.target, (long)s.ramped, s.hot ? 1 : 0);
    for (int k = 0; k < NB_VESC; k++) {
        const EscData &e = s.esc[k];
        ap("%s{\"id\":%d,\"ok\":%d,\"erpm\":%ld,\"duty\":%.3f,"
           "\"i_mot\":%.2f,\"i_in\":%.2f,\"v_in\":%.2f,"
           "\"t_fet\":%.1f,\"t_mot\":%.1f}",
           k ? "," : "", e.id, e.vivant ? 1 : 0, (long)e.erpm, fin(e.duty),
           fin(e.i_mot), fin(e.i_in), fin(e.v_in), fin(e.t_fet), fin(e.t_mot));
    }
    ap("],\"arme\":%d", s.arme ? 1 : 0);

    // --- Pilote ---
    ap(",\"pil\":{\"on\":%d,\"fr\":%d,\"ctl\":%d,\"esc\":%d,\"lev\":%d,\"raw\":%d}",
       s.pil.switch_on, s.pil.switch_fr, s.pil.switch_ctl, s.pil.esc_demarres,
       s.pil.levier, s.pil.levier_raw);

    // --- Xsens ---
    bool frais = (millis() - s.xs.t_ms) <= XSENS_DONNEES_TIMEOUT_MS;
    ap(",\"imu\":{\"ok\":%d,\"frais\":%d,\"roll\":%.2f,\"pitch\":%.2f,\"yaw\":%.2f,"
       "\"vok\":%d,\"v_kmh\":%.2f}",
       s.xs.att_valid, frais ? 1 : 0, fin(s.xs.roll), fin(s.xs.pitch), fin(s.xs.yaw),
       s.xs.vel_valid, fin(s.xs.speed * 3.6f));
    ap(",\"gps\":{\"ok\":%d,\"lat\":%.7f,\"lon\":%.7f,\"alt_ok\":%d,\"alt\":%.1f}",
       s.xs.pos_valid, fin(s.xs.lat), fin(s.xs.lon), s.xs.alt_valid, fin(s.xs.altitude));

#if FOILS_ACTIFS
    // --- Sonars + foils ---
    ap(",\"son\":{\"av\":%.1f,\"ag\":%.1f,\"ad\":%.1f}", fin(s.dist[0]), fin(s.dist[1]), fin(s.dist[2]));
    ap(",\"sio\":{\"av\":%d,\"ag\":%d,\"ad\":%d}", s.io[0], s.io[1], s.io[2]);
    ap(",\"foils\":{\"on\":%d,\"cmd\":{\"av\":%.1f,\"ag\":%.1f,\"ad\":%.1f},"
       "\"h\":{\"av\":%.2f,\"ag\":%.2f,\"ad\":%.2f},\"p\":%.2f,\"r\":%.2f}",
       s.run == RunMode::CONTROLE ? 1 : 0,
       fin(s.cmd[0]), fin(s.cmd[1]), fin(s.cmd[2]),
       fin(s.h[0]), fin(s.h[1]), fin(s.h[2]), fin(s.p_out), fin(s.r_out));
#endif

    // --- Watchdog : état des tâches ---
    ap(",\"wd\":{\"err\":%lu,\"rst\":%d", (unsigned long)s.err, s.rst ? 1 : 0);
    for (int i = 0; i < NB_TACHES; i++) {
        if (!s.wd[i].presente) continue;
        ap(",\"%s\":{\"ok\":%d,\"stk\":%lu,\"us\":%.0f}",
           NOMS_TACHES[i], s.wd[i].vivante ? 1 : 0,
           (unsigned long)s.wd[i].stack_libre, fin(s.us[i]));
    }
    ap("}}\n");

    if (jlen >= RPI_JSON_MAX - 1) return;   // trame tronquée : on ne l'envoie pas
    Serial.write((const uint8_t*)json, jlen);
}

static void sendHeartbeat()
{
    jlen = 0;
    ap("{\"type\":\"hb\",\"state\":\"idle\",\"t\":%lu}\n", (unsigned long)millis());
    Serial.write((const uint8_t*)json, jlen);
}

// Parsing volontairement minimal : on cherche la valeur de "cmd" (Real_V1).
static void handlePiCommand(const char* line)
{
    const char* p = strstr(line, "\"cmd\"");
    if (!p) return;

    if (strstr(p, "start")) {
        streaming = true;
        // Les gains PID arriveront ici plus tard (clé "pid").
    }
    else if (strstr(p, "stop")) {
        streaming  = false;
        lastBeatMs = millis();
        // NOTE : "stop" arrête seulement la télémétrie, pas les moteurs.
    }
}

// Lecture non bloquante des commandes du Pi (Real_V1)
static void readPiCommands()
{
    while (Serial.available()) {
        char c = Serial.read();

        if (c == '\r') continue;

        if (c == '\n') {
            piLine[piIdx] = '\0';
            if (piIdx > 0) handlePiCommand(piLine);
            piIdx = 0;
        }
        else if (piIdx < PI_LINE_MAX - 1) {
            piLine[piIdx++] = c;
        }
        else {
            piIdx = 0;   // ligne trop longue : on jette
        }
    }
}

// =====================================================
// Task_RPi — priorité 1 (basse), période 50 ms
// =====================================================
void Task_RPi(void *ptr)
{
  (void) ptr;
  TickType_t lastWakeTime = xTaskGetTickCount();

  while (1)
  {
    uint32_t t_debut = micros();

    // Commandes du Pi — non bloquant
    readPiCommands();

    // Sortie vers le Pi
    if (streaming) {
      sendTelemetry();                       // 20 Hz (PERIODE_RPI_MS)
    } else if (millis() - lastBeatMs >= HB_MS) {
      lastBeatMs = millis();
      sendHeartbeat();
    }

#if DEBUG_PRINT
    if (millis() - lastPrintMs >= PRINT_MS) {
      lastPrintMs = millis();
      Snapshot s = {};
      lireSnapshot(s);
      Serial.printf("[dbg] mode=%s run=%s arme=%d chaud=%d target=%d ramped=%.0f err=0x%lx\n",
                    modeStr(s.mode), runStr(s.run), s.arme ? 1 : 0, s.hot ? 1 : 0,
                    s.target, s.ramped, (unsigned long)s.err);
    }
#endif

#if DEBUG_LEVIER
    // Calibration du levier : app Pi FERMÉE, screen /dev/ttyACM0.
    // Note la valeur au repos -> LEVIER_RAW_MIN, à fond -> LEVIER_RAW_MAX
    if (millis() - lastPrintMs >= 200) {
      lastPrintMs = millis();
      PiloteData p = {};
      if (xSemaphoreTake(dataMutex, portMAX_DELAY)) { p = Pilote_data; xSemaphoreGive(dataMutex); }
      Serial.printf("[levier] raw=%d nettoye=%d\n", p.levier_raw, p.levier);
    }
#endif
    (void) lastPrintMs;

    rpi_temps_us = (float)(micros() - t_debut);   // écrit ici, lu ici : pas de mutex

    wd_vivant(T_RPI);
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_RPI_MS));
  }
}
