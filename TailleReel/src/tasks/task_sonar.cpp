// ⚠ En PREMIER (comme task_pilote.cpp) : avant arduino_freertos.h
#include <avr/eeprom.h>

#include "task_sonar.h"
#include "rtos_com.h"
#include "task_watchdog.h"
#include "task_pilote.h"     // Pilote_data.switch_on : zéro seulement switch ON coupé

// Définitions des variables
float distance_filt[NB_CANAUX] = {SONAR_DISTANCE_REF_AVANT, SONAR_DISTANCE_REF_ARRIERE_GAUCHE, SONAR_DISTANCE_REF_ARRIERE_DROIT};
int   PINS_SONARS[NB_CANAUX]   = {Analog_Sonar_Avant, Analog_Sonar_Arriere_Gauche, Analog_Sonar_Arriere_Droit};
int   PINS_SONARS_IO[NB_CANAUX] = {I_O_Sonar_Avant, I_O_Sonar_Arriere_Gauche, I_O_Sonar_Arriere_Droit};
static const float R_SONARS[NB_CANAUX] = {SONAR_R_AVANT, SONAR_R_ARRIERE_GAUCHE, SONAR_R_ARRIERE_DROIT};

float Sonar_distance[NB_CANAUX] = {SONAR_INIT_DISTANCE, SONAR_INIT_DISTANCE, SONAR_INIT_DISTANCE};
float Sonar_temps_us            = SONAR_INIT_TIME;
bool  Sonar_io[NB_CANAUX]       = {false, false, false};

float         Sonar_zero[NB_CANAUX] = {0.0f, 0.0f, 0.0f};
bool          Sonar_zero_ok         = false;
SonarZeroEtat Sonar_zero_etat       = {};

static_assert(SONAR_ZERO_DUREE_MS >= 1000, "SONAR_ZERO_DUREE_MS : au moins 1 s");
static_assert(SONAR_ZERO_ECART_MAX_CM > 0.0f, "SONAR_ZERO_ECART_MAX_CM doit etre > 0");

// Mutex
extern SemaphoreHandle_t dataMutex;

// =====================================================
//  Zéro : sauvegarde EEPROM
// =====================================================
#define ZERO_MAGIC 0x535A5231UL   // "SZR1"

struct ZeroEeprom {
    uint32_t magic;
    float    zero[NB_CANAUX];
    uint32_t somme;               // contrôle : XOR des octets des zéros ^ 0x5A5A5A5A
};

static uint32_t ZeroSomme(const float z[NB_CANAUX])
{
    uint32_t s = 0x5A5A5A5AUL, v;
    for (int i = 0; i < NB_CANAUX; i++) { memcpy(&v, &z[i], 4); s ^= v + 0x9E3779B9UL * (uint32_t)(i + 1); }
    return s;
}

// Zéro utilisable : dans la plage du sonar, et la hauteur de vol aussi
static bool ZeroDansPlage(int i, float z)
{
    return z > SONAR_DIST_MIN_CM + 0.5f && z < SONAR_DIST_MAX_CM - 0.5f &&
           z + H_VOL[i] <= SONAR_DIST_MAX_CM;
}

void Sonars_ChargerZero()
{
    ZeroEeprom c;
    eeprom_read_block(&c, (const void *)SONAR_ZERO_EEPROM_ADDR, sizeof(c));
    bool ok = (c.magic == ZERO_MAGIC && c.somme == ZeroSomme(c.zero));
    for (int i = 0; ok && i < NB_CANAUX; i++) ok = ZeroDansPlage(i, c.zero[i]);
    if (ok) {
        for (int i = 0; i < NB_CANAUX; i++) Sonar_zero[i] = c.zero[i];
    }
    Sonar_zero_ok = ok;
}

static void ZeroSauver(const float z[NB_CANAUX])
{
    ZeroEeprom c;
    c.magic = ZERO_MAGIC;
    for (int i = 0; i < NB_CANAUX; i++) c.zero[i] = z[i];
    c.somme = ZeroSomme(c.zero);
    eeprom_write_block(&c, (void *)SONAR_ZERO_EEPROM_ADDR, sizeof(c));
}

void SonarZero_Demander(bool switch_on)
{
#if SONARS_ACTIFS
    if (switch_on) {
        if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
            if (!Sonar_zero_etat.actif) Sonar_zero_etat.res = SZ_REFUS_ON;
            xSemaphoreGive(dataMutex);
        }
        return;
    }
    xEventGroupSetBits(egEtat, EVT_SONAR_ZERO);
#else
    (void) switch_on;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
        Sonar_zero_etat.res = SZ_SONARS_OFF;
        xSemaphoreGive(dataMutex);
    }
#endif
}

// Fonction lecture de sonar : distance filtrée (retournée, bornée) et brute (cm)
float lire_sonar(int i, float &dist_filt, float &brute)
{
  int raw = adc_lire(PINS_SONARS[i]);   // ADC partagé avec Task_Pilote
  float voltage = (raw / SONAR_ADC_BITS) * SONAR_VREF;
  float courant_mA = (voltage / R_SONARS[i]) * 1000.0f;
  float newDist = (courant_mA - SONAR_I_MIN_MA) / (SONAR_I_MAX_MA - SONAR_I_MIN_MA)
                  * (SONAR_DIST_MAX_CM - SONAR_DIST_MIN_CM) + SONAR_DIST_MIN_CM;
  brute = newDist;

  dist_filt = SONAR_ALPHA * newDist + (1.0f - SONAR_ALPHA) * dist_filt;

  if (dist_filt < SONAR_DIST_MIN_CM) dist_filt = SONAR_DIST_MIN_CM;
  if (dist_filt > SONAR_DIST_MAX_CM) dist_filt = SONAR_DIST_MAX_CM;

  return dist_filt;
}

// =====================================================
//  Mesure du zéro (appelée à chaque cycle de Task_LectureSonar)
// =====================================================
struct ZeroMesure {
    bool     actif;
    uint32_t debut;
    double   somme[NB_CANAUX];
    float    mn[NB_CANAUX], mx[NB_CANAUX];
    bool     invalide[NB_CANAUX];
    int      n;
};

// Retourne true si le zéro vient d'être enregistré (z = nouveaux zéros)
static bool ZeroCycle(ZeroMesure &m, bool demande, bool switch_on,
                      const float brute[NB_CANAUX], const bool io[NB_CANAUX],
                      uint32_t now, SonarZeroEtat &e, float z[NB_CANAUX])
{
    if (demande && !m.actif) {
        if (switch_on) { e.res = SZ_REFUS_ON; return false; }
        m = {};
        m.actif = true;
        m.debut = now;
        for (int i = 0; i < NB_CANAUX; i++) { m.mn[i] = 1e9f; m.mx[i] = -1e9f; }
        e.res = SZ_AUCUN;
        for (int i = 0; i < NB_CANAUX; i++) { e.res_s[i] = SZ_AUCUN; e.moy[i] = 0.0f; e.ecart[i] = 0.0f; }
    }
    e.actif = m.actif;
    if (!m.actif) { e.reste_ms = 0; return false; }

    if (switch_on) {                            // le bateau pourrait partir
        m.actif = false;
        e.actif = false; e.reste_ms = 0; e.res = SZ_ANNULE_ON;
        return false;
    }

    for (int i = 0; i < NB_CANAUX; i++) {
        m.somme[i] += brute[i];
        if (brute[i] < m.mn[i]) m.mn[i] = brute[i];
        if (brute[i] > m.mx[i]) m.mx[i] = brute[i];
        if (!io[i]) m.invalide[i] = true;
    }
    m.n++;

    uint32_t ecoule = now - m.debut;
    if (ecoule < SONAR_ZERO_DUREE_MS) {
        e.reste_ms = (uint16_t)(SONAR_ZERO_DUREE_MS - ecoule);
        return false;
    }

    // Fin de la mesure : résultat de chaque sonar
    m.actif = false;
    e.actif = false; e.reste_ms = 0;
    bool tous_ok = true;
    uint8_t pire = SZ_OK;
    for (int i = 0; i < NB_CANAUX; i++) {
        float moy = (float)(m.somme[i] / m.n);
        float ecart = m.mx[i] - moy;
        if (moy - m.mn[i] > ecart) ecart = moy - m.mn[i];
        e.moy[i] = moy;  e.ecart[i] = ecart;
        uint8_t r = SZ_OK;
        if (m.invalide[i])                         r = SZ_INVALIDE;
        else if (ecart > SONAR_ZERO_ECART_MAX_CM)  r = SZ_AGITE;
        else if (!ZeroDansPlage(i, moy))           r = SZ_HORS_PLAGE;
        e.res_s[i] = r;
        if (r != SZ_OK) {
            tous_ok = false;
            if (pire == SZ_OK || r < pire) pire = r;   // invalide > agité > hors plage
        }
        z[i] = moy;
    }
    e.res = tous_ok ? (uint8_t)SZ_OK : pire;
    return tous_ok;
}

// Tache de lecture des sonars
void Task_LectureSonar(void *ptr)
{
  (void) ptr;

  // Sortie TOR PNP : pull-down → fil débranché = non valide
  for(int i=0; i<NB_CANAUX; i++){
    pinMode(PINS_SONARS_IO[i], arduino::INPUT_PULLDOWN);
  }

  ZeroMesure    zm = {};
  SonarZeroEtat ze = {};
  bool          switch_on = true;      // relu sous mutex à chaque cycle

  TickType_t lastWakeTime = xTaskGetTickCount();

  while (1)
  {
    uint32_t t_debut = micros();

    float dist[NB_CANAUX];
    float brute[NB_CANAUX];
    bool  io[NB_CANAUX];

    for(int i=0; i<NB_CANAUX; i++){
      dist[i] = lire_sonar(i, distance_filt[i], brute[i]);
      distance_filt[i] = dist[i];
      io[i] = (digitalRead(PINS_SONARS_IO[i]) == SONAR_IO_VALIDE);
    }

    // Zéro sur l'eau (commande de l'UI)
    bool  demande = (xEventGroupClearBits(egEtat, EVT_SONAR_ZERO) & EVT_SONAR_ZERO) != 0;
    float z[NB_CANAUX];
    bool  nouveau = ZeroCycle(zm, demande, switch_on, brute, io, millis(), ze, z);
    if (nouveau) ZeroSauver(z);

    float duree_us = (float)(micros() - t_debut);

    if (xSemaphoreTake(dataMutex, portMAX_DELAY))
    {
      for(int i=0; i<NB_CANAUX; i++){
        Sonar_distance[i] = dist[i];
        Sonar_io[i]       = io[i];
      }
      Sonar_temps_us = duree_us;
      // Refus noté par SonarZero_Demander (Task_RPi, switch ON) : on le garde
      // tant qu'aucune nouvelle mesure n'a commencé.
      uint8_t r = Sonar_zero_etat.res;
      if ((r == SZ_REFUS_ON || r == SZ_SONARS_OFF) && !demande && !ze.actif) ze.res = r;
      Sonar_zero_etat = ze;
      if (nouveau) {
        for (int i = 0; i < NB_CANAUX; i++) Sonar_zero[i] = z[i];
        Sonar_zero_ok = true;
      }
      switch_on = Pilote_data.switch_on;
      xSemaphoreGive(dataMutex);
    }

    wd_vivant(T_SONAR);
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_SONAR_MS));
  }
}
