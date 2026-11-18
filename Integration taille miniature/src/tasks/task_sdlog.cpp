// =====================================================
//  task_sdlog.cpp
//  Journalisation JSON Lines sur la carte SD du Teensy 4.1.
//  Voir task_sdlog.h pour l'architecture (2 tâches + file RAM).
// =====================================================

#include "task_sdlog.h"
#include <SD.h>
#include <message_buffer.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "task_xsens.h"
#include "task_sonar.h"
#include "task_foils_control.h"
#include "task_propulsion.h"
#include "task_state_machine.h"

extern SemaphoreHandle_t dataMutex;

// ─── État publié (voir task_sdlog.h) ───
SDLogEtat SDLog_etat         = SDLogEtat::INIT;
uint16_t  SDLog_fichier_no   = 0;
uint32_t  SDLog_lignes       = 0;
uint32_t  SDLog_sans_fichier = 0;
uint32_t  SDLog_err_ecriture = 0;
uint32_t  SDLog_perdues      = 0;
float     SDLog_temps_us     = 0.0f;

// ─── File RAM entre les deux tâches (allocation statique) ───
// Messages de contrôle : 1 octet. Les trames JSON commencent par '{'.
static const char CTRL_OUVRIR = 0x01;   // entrée en RUN  → nouveau fichier
static const char CTRL_FERMER = 0x02;   // sortie de RUN → fermer le fichier

static uint8_t               s_mbStockage[SDLOG_TAMPON_OCTETS + 1];
static StaticMessageBuffer_t s_mbStruct;
static MessageBufferHandle_t s_mb = nullptr;

void SDLog_init()
{
    if (s_mb == nullptr) {
        s_mb = xMessageBufferCreateStatic(SDLOG_TAMPON_OCTETS, s_mbStockage, &s_mbStruct);
    }
}

const char* SDLog_etatStr(SDLogEtat e)
{
    switch (e) {
        case SDLogEtat::INIT:         return "INIT";
        case SDLogEtat::PAS_DE_CARTE: return "PAS_DE_CARTE";
        case SDLogEtat::PRET:         return "PRET";
        case SDLogEtat::ENREGISTRE:   return "ENREGISTRE";
        case SDLogEtat::ERREUR:       return "ERREUR";
    }
    return "?";
}

static const char* topStr(TopState s)
{
    switch (s) {
        case TopState::IDLE: return "IDLE";
        case TopState::RUN:  return "RUN";
        case TopState::STOP: return "STOP";
    }
    return "?";
}

static const char* runStr(RunState s)
{
    switch (s) {
        case RunState::NEUTRE:   return "NEUTRE";
        case RunState::AVANCE:   return "AVANCE";
        case RunState::RECULE:   return "RECULE";
        case RunState::CONTROLE: return "CONTROLE";
    }
    return "?";
}

// NaN / inf casseraient le JSON
static inline double fin(double v) { return std::isfinite(v) ? v : 0.0; }

// =====================================================
//  Formatage JSON (buffer de la tâche d'échantillonnage)
// =====================================================
static char s_ligne[SDLOG_LIGNE_MAX];
static int  s_len = 0;

static void ap(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void ap(const char* fmt, ...)
{
    if (s_len >= SDLOG_LIGNE_MAX - 1) return;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(s_ligne + s_len, SDLOG_LIGNE_MAX - s_len, fmt, args);
    va_end(args);
    if (n > 0) s_len += n;
    if (s_len > SDLOG_LIGNE_MAX - 1) s_len = SDLOG_LIGNE_MAX - 1;
}

// Retourne la longueur, ou 0 si la trame a été tronquée (on ne l'envoie pas)
static int finLigne()
{
    ap("}\n");
    return (s_len >= SDLOG_LIGNE_MAX - 1) ? 0 : s_len;
}

// ─── Copie locale des données partagées ───
struct Snap {
    TopState top;  RunState run;  bool foils_on;  float us_sm;
    bool  rc_ok;  float thr;  float rud;  bool swA;  bool swC;  float us_prop;
    XsensData xs;
    float dist[NB_CANAUX];  float us_son;
    float cmd[NB_CANAUX];   float h[NB_CANAUX];  float hi[NB_CANAUX];
    float p_out;  float r_out;  float r_ref;  float ri;  float us_foils;
};

static void lireSnapshot(Snap &s)
{
    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
        s.top      = SM_top_state;
        s.run      = SM_run_state;
        s.foils_on = SM_foil_control_active;
        s.us_sm    = SM_control_time_us;

        s.rc_ok    = Prop_rc_valid;
        s.thr      = Prop_rc_throttle;
        s.rud      = Prop_rc_rudder;
        s.swA      = Prop_rc_switchA;
        s.swC      = Prop_rc_switchC;
        s.us_prop  = Prop_control_time_us;

        s.xs       = Xsens_data;

        for (int i = 0; i < NB_CANAUX; i++) {
            s.dist[i] = Sonar_distance[i];
            s.cmd[i]  = HPR_cmd_servos[i];
            s.h[i]    = H_outputs[i];
            s.hi[i]   = H_integral_out[i];
        }
        s.us_son   = Sonar_temps_us;
        s.p_out    = P_output;
        s.r_out    = R_output;
        s.r_ref    = R_ref_output;
        s.ri       = R_integral_out;
        s.us_foils = HPR_control_time_us;
        xSemaphoreGive(dataMutex);
    }
}

// 1re ligne de chaque fichier : réglages en vigueur pendant l'essai
static int formaterEntete(uint32_t t_ms)
{
    s_len = 0;
    ap("{\"type\":\"config\",\"t\":%lu,\"fw\":\"%s %s\"", (unsigned long)t_ms, __DATE__, __TIME__);
    ap(",\"axes\":{\"h\":%d,\"p\":%d,\"r\":%d,\"r_ref_rudder\":%d,\"foils_en\":%d}",
       FOIL_HAUTEUR_ACTIVE, FOIL_PITCH_ACTIVE, FOIL_ROLL_ACTIVE, R_REF_FROM_RUDDER,
       MINIATURE_FOILS_ENABLED);
    ap(",\"servo\":{\"alpha\":%.3f,\"neutre\":[%d,%d,%d],\"min\":[%d,%d,%d],\"max\":[%d,%d,%d]}",
       (double)SERVO_ALPHA,
       SERVO_AVANT_NEUTRAL, SERVO_ARRIERE_GAUCHE_NEUTRAL, SERVO_ARRIERE_DROIT_NEUTRAL,
       SERVO_AVANT_MIN, SERVO_ARRIERE_GAUCHE_MIN, SERVO_ARRIERE_DROIT_MIN,
       SERVO_AVANT_MAX, SERVO_ARRIERE_GAUCHE_MAX, SERVO_ARRIERE_DROIT_MAX);
    ap(",\"h\":{\"ref\":[%.1f,%.1f,%.1f],\"db\":%.2f,\"dbd\":%.3f,\"out_max_ar\":%.1f,"
       "\"kp\":[%.3f,%.3f,%.3f],\"ki\":[%.3f,%.3f,%.3f],\"kd\":[%.3f,%.3f,%.3f]}",
       (double)H_DISTANCE_REF_AVANT, (double)H_DISTANCE_REF_ARRIERE_GAUCHE, (double)H_DISTANCE_REF_ARRIERE_DROIT,
       (double)H_DEADBAND_ERR, (double)H_DEADBAND_DERIV, (double)H_ARRIERE_OUT_MAX,
       (double)H_KP_BAS, (double)H_KP_MID, (double)H_KP_HAUT,
       (double)H_KI_BAS, (double)H_KI_MID, (double)H_KI_HAUT,
       (double)H_KD_BAS, (double)H_KD_MID, (double)H_KD_HAUT);
    ap(",\"r\":{\"ref\":%.2f,\"db\":%.2f,\"dbd\":%.3f,"
       "\"kp\":[%.3f,%.3f,%.3f],\"ki\":[%.3f,%.3f,%.3f],\"kd\":[%.3f,%.3f,%.3f]}",
       (double)R_REF_DEG, (double)R_DEADBAND_ERR, (double)R_DEADBAND_DERIV,
       (double)R_KP_BAS, (double)R_KP_MID, (double)R_KP_HAUT,
       (double)R_KI_BAS, (double)R_KI_MID, (double)R_KI_HAUT,
       (double)R_KD_BAS, (double)R_KD_MID, (double)R_KD_HAUT);
    ap(",\"sm\":{\"seuil_haut\":%.3f,\"seuil_bas\":%.3f}",
       (double)SM_SPEED_THRESHOLD_HIGH, (double)SM_SPEED_THRESHOLD_LOW);
    return finLigne();
}

// Trame de données (20 Hz)
static int formaterLigne(const Snap &s, uint32_t t_ms, float us_log, uint32_t perdues)
{
    s_len = 0;
    ap("{\"t\":%lu,\"mode\":\"%s\",\"run\":\"%s\",\"foils_on\":%d",
       (unsigned long)t_ms, topStr(s.top), runStr(s.run), s.foils_on ? 1 : 0);

    // --- Manette ---
    ap(",\"rc\":{\"ok\":%d,\"thr\":%.3f,\"rud\":%.3f,\"a\":%d,\"c\":%d}",
       s.rc_ok ? 1 : 0, fin(s.thr), fin(s.rud), s.swA ? 1 : 0, s.swC ? 1 : 0);

    // --- Xsens : attitude + vitesse ---
    bool frais = (t_ms - s.xs.t_ms) <= XSENS_DONNEES_TIMEOUT_MS;
    ap(",\"imu\":{\"ok\":%d,\"frais\":%d,\"roll\":%.2f,\"pitch\":%.2f,\"yaw\":%.2f,"
       "\"vok\":%d,\"v_kmh\":%.2f,\"vx\":%.3f,\"vy\":%.3f,\"vz\":%.3f,\"vrej\":%lu}",
       s.xs.att_valid ? 1 : 0, frais ? 1 : 0,
       fin(s.xs.roll), fin(s.xs.pitch), fin(s.xs.yaw),
       s.xs.vel_valid ? 1 : 0, fin(s.xs.speed * 3.6f),
       fin(s.xs.vx), fin(s.xs.vy), fin(s.xs.vz), (unsigned long)s.xs.v_rejets);

    // --- GPS ---
    ap(",\"gps\":{\"ok\":%d,\"lat\":%.7f,\"lon\":%.7f,\"alt_ok\":%d,\"alt\":%.1f,\"prej\":%lu}",
       s.xs.pos_valid ? 1 : 0, fin(s.xs.lat), fin(s.xs.lon),
       s.xs.alt_valid ? 1 : 0, fin(s.xs.altitude), (unsigned long)s.xs.pos_rejets);

    // --- Sonars (un seul sonar arrière, centré : canaux 1 et 2 identiques) ---
    ap(",\"son\":{\"av\":%.1f,\"ar\":%.1f}", fin(s.dist[0]), fin(s.dist[1]));

    // --- Foils ---
    ap(",\"foils\":{\"cmd\":{\"av\":%.2f,\"ag\":%.2f,\"ad\":%.2f},"
       "\"h\":{\"av\":%.2f,\"ag\":%.2f,\"ad\":%.2f},"
       "\"hi\":{\"av\":%.2f,\"ar\":%.2f},"
       "\"p\":%.2f,\"r\":%.2f,\"rref\":%.2f,\"ri\":%.2f}",
       fin(s.cmd[0]), fin(s.cmd[1]), fin(s.cmd[2]),
       fin(s.h[0]), fin(s.h[1]), fin(s.h[2]),
       fin(s.hi[0]), fin(s.hi[1]),
       fin(s.p_out), fin(s.r_out), fin(s.r_ref), fin(s.ri));

    // --- Durées d'exécution des tâches [µs] ---
    ap(",\"us\":{\"foils\":%.0f,\"son\":%.0f,\"xs\":%.0f,\"prop\":%.0f,\"sm\":%.0f,\"log\":%.0f}",
       fin(s.us_foils), fin(s.us_son), fin(s.xs.temps_us), fin(s.us_prop), fin(s.us_sm), fin(us_log));

    // --- Santé du journal ---
    ap(",\"log\":{\"perdu\":%lu}", (unsigned long)perdues);

    return finLigne();
}

// =====================================================
//  Task_SDLog_Echantillon — 20 Hz, ne touche jamais à la SD
// =====================================================
static bool envoyerCtrl(char c)
{
    return xMessageBufferSend(s_mb, &c, 1, pdMS_TO_TICKS(5)) == 1;
}

void Task_SDLog_Echantillon(void *ptr)
{
    (void) ptr;
    SDLog_init();   // sécurité si oubliée dans setup()

    bool     session  = false;   // fichier demandé à l'écriture (OUVRIR envoyé)
    uint32_t perdues  = 0;
    float    us_log   = 0.0f;

    TickType_t lastWakeTime = xTaskGetTickCount();

    while (1)
    {
        uint32_t t_debut = micros();
        uint32_t t_ms    = millis();

        Snap s = {};
        lireSnapshot(s);
        bool en_run = (s.top == TopState::RUN);

        // Entrée en RUN : nouveau fichier + ligne de configuration
        if (en_run && !session) {
            if (envoyerCtrl(CTRL_OUVRIR)) {
                session = true;
                int n = formaterEntete(t_ms);
                if (n > 0 && xMessageBufferSend(s_mb, s_ligne, n, 0) != (size_t)n) perdues++;
            }
        }

        if (session) {
            // En RUN : une trame par période. À la sortie de RUN, une dernière
            // trame (état STOP/IDLE, rc.ok…) pour voir POURQUOI on est sorti.
            int n = formaterLigne(s, t_ms, us_log, perdues);
            if (n == 0 || xMessageBufferSend(s_mb, s_ligne, n, 0) != (size_t)n) perdues++;

            if (!en_run && envoyerCtrl(CTRL_FERMER)) session = false;
        }

        us_log = (float)(micros() - t_debut);

        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            SDLog_perdues  = perdues;
            SDLog_temps_us = us_log;
            xSemaphoreGive(dataMutex);
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_SDLOG_MS));
    }
}

// =====================================================
//  Task_SDLog_Ecriture — basse priorité, seule à toucher la SD
// =====================================================
static File      s_fichier;
static bool      s_carte_ok       = false;
static bool      s_fichier_ouvert = false;
static bool      s_session        = false;   // entre OUVRIR et FERMER
static uint16_t  s_prochain_no    = 1;
static uint16_t  s_fichier_no     = 0;
static uint32_t  s_lignes         = 0;
static uint32_t  s_sans_fichier   = 0;
static uint32_t  s_err            = 0;
static SDLogEtat s_etat           = SDLogEtat::INIT;
static uint32_t  s_dernier_essai  = 0;
static uint32_t  s_dernier_flush  = 0;

static char      s_msg[SDLOG_LIGNE_MAX];      // message reçu de la file
static char      s_bloc[SDLOG_BLOC_OCTETS];   // accumulé avant écriture
static size_t    s_bloc_n = 0;
static char      s_entete[SDLOG_LIGNE_MAX];   // copie de la ligne config (réouverture)
static size_t    s_entete_n = 0;

// Initialise la carte et cherche le prochain numéro libre (run_XXXX.jsonl)
static bool carteInit()
{
    if (!SD.begin(BUILTIN_SDCARD)) return false;

    unsigned max_no = 0;
    File racine = SD.open("/");
    if (racine) {
        while (true) {
            File e = racine.openNextFile();
            if (!e) break;
            unsigned n = 0;
            if (!e.isDirectory() && sscanf(e.name(), "run_%u.jsonl", &n) == 1 && n > max_no && n < 10000) {
                max_no = n;
            }
            e.close();
        }
        racine.close();
    }
    s_prochain_no = (uint16_t)(max_no + 1);
    return true;
}

static void fermerFichier()
{
    if (s_fichier_ouvert) s_fichier.close();
    s_fichier_ouvert = false;
    s_bloc_n = 0;
}

// Écrit le bloc accumulé. En cas d'échec : fichier fermé, carte à réinitialiser.
static bool viderBloc()
{
    if (!s_fichier_ouvert || s_bloc_n == 0) return true;
    size_t n = s_bloc_n;
    size_t w = s_fichier.write(s_bloc, n);
    s_bloc_n = 0;
    if (w != n) {
        s_err++;
        fermerFichier();
        s_carte_ok = false;
        s_etat = SDLogEtat::ERREUR;
        return false;
    }
    return true;
}

static void ajouter(const char* data, size_t len)
{
    if (s_bloc_n + len > sizeof(s_bloc)) {
        if (!viderBloc()) return;
    }
    memcpy(s_bloc + s_bloc_n, data, len);
    s_bloc_n += len;
}

// Ouvre un nouveau fichier (réinitialise la carte au besoin)
static void ouvrirFichier(uint32_t now)
{
    s_dernier_essai = now;
    if (!s_carte_ok) s_carte_ok = carteInit();
    if (!s_carte_ok) { s_etat = SDLogEtat::PAS_DE_CARTE; return; }

    char nom[24];
    snprintf(nom, sizeof(nom), "run_%04u.jsonl", (unsigned)s_prochain_no);
    s_fichier = SD.open(nom, FILE_WRITE);
    if (!s_fichier) {
        s_err++;
        s_carte_ok = false;
        s_etat = SDLogEtat::ERREUR;
        return;
    }
    s_fichier_no     = s_prochain_no++;
    s_fichier_ouvert = true;
    s_bloc_n         = 0;
    s_lignes         = 0;
    s_dernier_flush  = now;
    s_etat           = SDLogEtat::ENREGISTRE;

    // Réouverture en cours de session (carte insérée en retard, erreur) :
    // on remet la ligne de configuration en tête du nouveau fichier.
    if (s_entete_n > 0) ajouter(s_entete, s_entete_n);
}

void Task_SDLog_Ecriture(void *ptr)
{
    (void) ptr;
    SDLog_init();

    // Essai au démarrage, pour annoncer l'état de la carte dans la télémétrie
    s_carte_ok = carteInit();
    s_etat = s_carte_ok ? SDLogEtat::PRET : SDLogEtat::PAS_DE_CARTE;

    while (1)
    {
        size_t len = xMessageBufferReceive(s_mb, s_msg, sizeof(s_msg), pdMS_TO_TICKS(100));
        uint32_t now = millis();

        if (len == 1 && s_msg[0] == CTRL_OUVRIR) {
            fermerFichier();          // sécurité (FERMER perdu)
            s_session  = true;
            s_entete_n = 0;
            ouvrirFichier(now);
        }
        else if (len == 1 && s_msg[0] == CTRL_FERMER) {
            if (viderBloc() && s_fichier_ouvert) s_fichier.flush();
            fermerFichier();
            s_session = false;
            if (s_carte_ok) s_etat = SDLogEtat::PRET;
        }
        else if (len > 1) {
            bool entete = (strncmp(s_msg, "{\"type\":\"config\"", 16) == 0);
            if (entete) {
                memcpy(s_entete, s_msg, len);
                s_entete_n = len;
            }
            if (s_fichier_ouvert) {
                ajouter(s_msg, len);
                if (!entete) s_lignes++;
            } else if (!entete) {
                s_sans_fichier++;
            }
        }

        // Carte absente ou en erreur pendant une session : nouvel essai
        if (s_session && !s_fichier_ouvert && (now - s_dernier_essai) >= SDLOG_RETRY_MS) {
            ouvrirFichier(now);
        }

        // Flush périodique : limite la perte si le courant est coupé
        if (s_fichier_ouvert && (now - s_dernier_flush) >= SDLOG_FLUSH_MS) {
            if (viderBloc()) s_fichier.flush();
            s_dernier_flush = now;
        }

        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            SDLog_etat         = s_etat;
            SDLog_fichier_no   = s_fichier_no;
            SDLog_lignes       = s_lignes;
            SDLog_sans_fichier = s_sans_fichier;
            SDLog_err_ecriture = s_err;
            xSemaphoreGive(dataMutex);
        }
    }
}
