// =====================================================
//  servos.cpp — PWM des servos en µs + calibration EEPROM
//  (voir servos.h)
// =====================================================

// ⚠ En PREMIER (comme dans task_pilote.cpp) : avant arduino_freertos.h
#include <avr/eeprom.h>

#include "servos.h"
#include <semphr.h>
#include <stdlib.h>
#include <string.h>

static const int PINS[NB_CANAUX] = {PWM_Servo_Avant, PWM_Servo_Arriere_Gauche, PWM_Servo_Arriere_Droit};

static const int CENTRES[NB_CANAUX] = {SERVO_US_CENTRE_AVANT, SERVO_US_CENTRE_ARRIERE_GAUCHE, SERVO_US_CENTRE_ARRIERE_DROIT};
static_assert(SERVO_US_ABS_MIN < SERVO_US_CENTRE_AVANT && SERVO_US_CENTRE_AVANT < SERVO_US_ABS_MAX &&
              SERVO_US_ABS_MIN < SERVO_US_CENTRE_ARRIERE_GAUCHE && SERVO_US_CENTRE_ARRIERE_GAUCHE < SERVO_US_ABS_MAX &&
              SERVO_US_ABS_MIN < SERVO_US_CENTRE_ARRIERE_DROIT && SERVO_US_CENTRE_ARRIERE_DROIT < SERVO_US_ABS_MAX,
              "SERVO_US_CENTRE_x doit etre entre SERVO_US_ABS_MIN et SERVO_US_ABS_MAX");
static_assert(SERVO_CALIB_PAS_MAX > 0, "SERVO_CALIB_PAS_MAX doit etre > 0");

// ─── Calibration en service (protégée par mtx : lue par Task_foils_Control) ───
static uint16_t          cal[NB_CANAUX][3];      // Min / Neutre / Max, 0 = pas enregistré
static SemaphoreHandle_t mtx = NULL;

// ─── État de la calibration (utilisé par Task_RPi seulement ; actif lu par les foils) ───
static volatile int8_t actif = -1;
static int      pos      = SERVO_US_CENTRE_AVANT; // µs commandés au servo en calibration
static uint32_t vie_ms   = 0;
static uint8_t  res      = SCR_AUCUN;
static uint8_t  derniere = SV_NEUTRE;

// =====================================================
//  EEPROM
// =====================================================
#define SERVO_MAGIC 0x53525631UL   // "SRV1"

struct ServoCalEeprom {
    uint32_t magic;
    uint16_t us[NB_CANAUX][3];
    uint16_t somme;                // contrôle : XOR de toutes les valeurs ^ 0x5A5A
};

static uint16_t Somme(const uint16_t us[NB_CANAUX][3])
{
    uint16_t s = 0x5A5A;
    for (int i = 0; i < NB_CANAUX; i++)
        for (int k = 0; k < 3; k++) s ^= (uint16_t)(us[i][k] + 31 * (3 * i + k));
    return s;
}

static void Charger()
{
    ServoCalEeprom c;
    eeprom_read_block(&c, (const void *)SERVO_CALIB_EEPROM_ADDR, sizeof(c));
    bool ok = (c.magic == SERVO_MAGIC && c.somme == Somme(c.us));
    for (int i = 0; i < NB_CANAUX; i++)
        for (int k = 0; k < 3; k++) {
            uint16_t v = ok ? c.us[i][k] : 0;
            cal[i][k] = (v >= SERVO_US_ABS_MIN && v <= SERVO_US_ABS_MAX) ? v : 0;
        }
}

static void Sauver(const uint16_t us[NB_CANAUX][3])
{
    ServoCalEeprom c;
    c.magic = SERVO_MAGIC;
    memcpy(c.us, us, sizeof(c.us));
    c.somme = Somme(c.us);
    eeprom_write_block(&c, (void *)SERVO_CALIB_EEPROM_ADDR, sizeof(c));
}

// =====================================================
//  Utilitaires
// =====================================================
static bool Verrou()   { return mtx == NULL || xSemaphoreTake(mtx, portMAX_DELAY) == pdTRUE; }
static void Deverrou() { if (mtx) xSemaphoreGive(mtx); }

static bool ValideV(const uint16_t v[3])
{
    return v[SV_MIN] >= SERVO_US_ABS_MIN && v[SV_MIN] < v[SV_NEUTRE] &&
           v[SV_NEUTRE] < v[SV_MAX] && v[SV_MAX] <= SERVO_US_ABS_MAX;
}

static int Borne(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Valeur entière qui suit la clé (ex. "\"s\"") dans la ligne, ou def
static int LireInt(const char* ligne, const char* cle, int def)
{
    const char* p = strstr(ligne, cle);
    if (!p) return def;
    p = strchr(p + strlen(cle), ':');
    if (!p) return def;
    return atoi(p + 1);
}

// "k": "min" / "neutre" / "max" → ServoVal, ou -1
static int LireK(const char* ligne)
{
    const char* p = strstr(ligne, "\"k\"");
    if (!p) return -1;
    p += 3;
    if (strstr(p, "neutre")) return SV_NEUTRE;
    if (strstr(p, "min"))    return SV_MIN;
    if (strstr(p, "max"))    return SV_MAX;
    return -1;
}

// =====================================================
//  Sortie PWM
// =====================================================
void Servo_Ecrire(int i, float us)
{
    if (i < 0 || i >= NB_CANAUX) return;
    if (us < SERVO_US_ABS_MIN) us = SERVO_US_ABS_MIN;
    if (us > SERVO_US_ABS_MAX) us = SERVO_US_ABS_MAX;
    // rapport cyclique = us / période (20 000 µs à 50 Hz), sur SERVO_PWM_BITS bits
    uint32_t duty = (uint32_t)(us * (float)SERVO_PWM_HZ * (float)(1UL << SERVO_PWM_BITS) / 1000000.0f + 0.5f);
    analogWrite(PINS[i], duty);
}

void Servo_Couper(int i)
{
    if (i < 0 || i >= NB_CANAUX) return;
    analogWrite(PINS[i], 0);
}

void Servos_Init()
{
    mtx = xSemaphoreCreateMutex();
    analogWriteResolution(SERVO_PWM_BITS);   // seuls les servos utilisent analogWrite
    for (int i = 0; i < NB_CANAUX; i++) {
        pinMode(PINS[i], arduino::OUTPUT);
        digitalWrite(PINS[i], 0);            // pas d'impulsion tant que rien n'est commandé
        analogWriteFrequency(PINS[i], SERVO_PWM_HZ);
    }
    Charger();
}

// =====================================================
//  Calibration en service
// =====================================================
bool Servo_Valide(int i)
{
    if (i < 0 || i >= NB_CANAUX) return false;
    bool v = false;
    if (Verrou()) { v = ValideV(cal[i]); Deverrou(); }
    return v;
}

bool Servos_TousValides()
{
    for (int i = 0; i < NB_CANAUX; i++)
        if (!Servo_Valide(i)) return false;
    return true;
}

bool Servo_Bornes(int i, float &mn, float &neutre, float &mx)
{
    if (i < 0 || i >= NB_CANAUX) return false;
    bool v = false;
    if (Verrou()) {
        v = ValideV(cal[i]);
        mn = cal[i][SV_MIN];  neutre = cal[i][SV_NEUTRE];  mx = cal[i][SV_MAX];
        Deverrou();
    }
    return v;
}

// =====================================================
//  Calibration (Task_RPi)
// =====================================================
bool ServoCalib_Active() { return actif >= 0; }

// Fin de la calibration : le servo va à son neutre s'il est valide,
// sinon plus d'impulsion. (Avec FOILS_ACTIFS, Task_foils_Control
// reprend la main au cycle suivant.)
static void Fin(uint8_t r)
{
    int s = actif;
    actif = -1;
    res   = r;
    if (s < 0) return;
    float mn, neu, mx;
    if (Servo_Bornes(s, mn, neu, mx)) Servo_Ecrire(s, neu);
    else                              Servo_Couper(s);
}

void ServoCalib_Commande(const char* ligne, bool switch_on)
{
    uint32_t now = millis();

    if (strstr(ligne, "servo_calib")) {
        int s = LireInt(ligne, "\"s\"", -1);
        if (s < 0 || s >= NB_CANAUX) return;
        if (switch_on) {                    // le bateau pourrait être piloté
            if (actif >= 0) Fin(SCR_FIN_ON);
            res = SCR_REFUS_ON;
            return;
        }
        // Départ : neutre déjà enregistré (reprise), sinon SERVO_US_CENTRE_x
        uint16_t neu = 0;
        if (Verrou()) { neu = cal[s][SV_NEUTRE]; Deverrou(); }
        pos    = neu ? neu : CENTRES[s];     // SERVO_US_CENTRE_x (config.h)
        actif  = (int8_t)s;
        vie_ms = now;
        res    = SCR_AUCUN;
        // Les autres servos : à leur neutre s'ils sont valides, sinon libres
        for (int i = 0; i < NB_CANAUX; i++) {
            if (i == s) continue;
            float mn, n, mx;
            if (Servo_Bornes(i, mn, n, mx)) Servo_Ecrire(i, n);
            else                            Servo_Couper(i);
        }
        Servo_Ecrire(s, (float)pos);
        return;
    }

    if (actif < 0) return;                  // pas de calibration en cours : on ignore
    vie_ms = now;                           // toute commande servo_* = signe de vie

    if (strstr(ligne, "servo_pas")) {
        int d = Borne(LireInt(ligne, "\"d\"", 0), -SERVO_CALIB_PAS_MAX, SERVO_CALIB_PAS_MAX);
        pos = Borne(pos + d, SERVO_US_ABS_MIN, SERVO_US_ABS_MAX);
        Servo_Ecrire(actif, (float)pos);
    }
    else if (strstr(ligne, "servo_enr")) {
        int k = LireK(ligne);
        if (k < 0) return;
        uint16_t copie[NB_CANAUX][3];
        if (Verrou()) {
            cal[actif][k] = (uint16_t)pos;
            memcpy(copie, cal, sizeof(copie));
            Deverrou();
        }
        Sauver(copie);
        res      = SCR_ENREGISTRE;
        derniere = (uint8_t)k;
    }
    else if (strstr(ligne, "servo_fin")) {
        Fin(SCR_FIN);
    }
    // "servo_vie" : rien d'autre
}

void ServoCalib_Tick(bool switch_on)
{
    if (actif < 0) return;
    if (switch_on) {
        Fin(SCR_FIN_ON);
    } else if (millis() - vie_ms > SERVO_CALIB_VIE_MS) {
        Fin(SCR_FIN_VIE);
    } else {
        Servo_Ecrire(actif, (float)pos);    // réaffirme la position
    }
}

void ServoCalib_Etat(ServoEtat &e)
{
    e.actif    = actif;
    e.us       = (uint16_t)pos;
    e.res      = res;
    e.derniere = derniere;
    if (Verrou()) {
        memcpy(e.cal, cal, sizeof(e.cal));
        for (int i = 0; i < NB_CANAUX; i++) e.valide[i] = ValideV(cal[i]);
        Deverrou();
    }
}
