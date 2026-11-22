// =====================================================
//  test_servos.cpp — test unitaire des 3 servos (PCB + DC-DC)
//
//  Programme À PART du firmware : environnement PlatformIO
//  « test_servos » (platformio.ini). Le firmware normal
//  (env teensy41) ne compile pas ce dossier.
//
//    Compiler + flasher : pio run -e test_servos -t upload
//    Moniteur série     : pio device monitor -e test_servos
//                         (ou le moniteur de VS Code, 115200)
//
//  Servos Hitec MDB1200WP (fiche HCS v2.2) :
//    impulsion 900 à 2100 µs, centre 1500 µs, ±60° par défaut,
//    50 Hz, alimentation 4,0 à 8,4 V.
//    Courant : ~0,5 A à vide, jusqu'à 5 A bloqué (6 V) par servo
//    → 3 servos qui démarrent ensemble = gros pic pour le DC-DC.
//
//  Broches (config.h) : avant = 2, arrière gauche = 3, arrière droit = 4.
//  La pin 32 (DC_DC_3V3) n'est PAS touchée : DC-DC toujours alimenté.
//
//  Au démarrage : les 3 servos au centre, puis balayage lent auto.
//  Commandes (moniteur série, une par ligne) :
//    ?          aide
//    a          balayage auto ON / OFF
//    m          mode du balayage : lent (rampe) / saut (butée à butée,
//               les 3 servos en même temps : pire cas pour le DC-DC)
//    s N        servo commandé à la main : 0 avant, 1 arr. gauche,
//               2 arr. droit, 3 tous (défaut)
//    u N        position en µs (900 à 2100)        ex. u 1800
//    d N        position en degrés (-60 à +60)      ex. d -30
//    c          centre (1500 µs)
//    x          coupe le signal PWM (servos relâchés)
//  Une commande u / d / c / x arrête le balayage auto.
// =====================================================

#include <arduino_freertos.h>
#include "config.h"

// ─── Fiche du servo (MDB1200WP) ───
static const int US_MIN    = 900;
static const int US_MAX    = 2100;
static const int US_CENTRE = 1500;
static const int DEG_MAX   = 60;          // course par défaut : ±60°

// ─── PWM 50 Hz ───
static const int   PWM_HZ   = 50;
static const int   PWM_BITS = 15;         // 15 bits à 50 Hz : 0,6 µs par pas
static const float PERIODE_US = 1000000.0f / PWM_HZ;

// ─── Balayage ───
static const int   PERIODE_MS     = 20;   // boucle à 50 Hz (une impulsion par cycle)
static const int   LENT_PAS_US    = 12;   // µs par cycle : 900 → 2100 en 2 s
static const int   SAUT_PAUSE_MS  = 1500; // temps en butée avant le saut suivant
static const int   AFFICHAGE_MS   = 1000;

static const int   NB_SERVOS = 3;
static const int   PINS[NB_SERVOS]  = {PWM_Servo_Avant, PWM_Servo_Arriere_Gauche, PWM_Servo_Arriere_Droit};
static const char* NOMS[NB_SERVOS]  = {"avant", "arr. gauche", "arr. droit"};

static int  pos_us[NB_SERVOS] = {US_CENTRE, US_CENTRE, US_CENTRE};
static bool actif[NB_SERVOS]  = {false, false, false};   // false = pas de signal
static bool balayage = true;
static bool mode_saut = false;
static int  choix = 3;                                    // 3 = tous

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Envoie la position (µs) au servo i ; borne toujours à la plage de la fiche.
static void ecrire_us(int i, int us)
{
    us = clampi(us, US_MIN, US_MAX);
    pos_us[i] = us;
    actif[i]  = true;
    uint32_t duty = (uint32_t)((float)us / PERIODE_US * (float)(1UL << PWM_BITS) + 0.5f);
    analogWrite(PINS[i], duty);
}

static void couper(int i)
{
    actif[i] = false;
    analogWrite(PINS[i], 0);        // plus d'impulsion : le servo est relâché
}

static void appliquer(int us)       // au(x) servo(s) choisi(s)
{
    for (int i = 0; i < NB_SERVOS; i++)
        if (choix == 3 || choix == i) ecrire_us(i, us);
}

static int deg_vers_us(int deg)
{
    deg = clampi(deg, -DEG_MAX, DEG_MAX);
    return US_CENTRE + deg * (US_MAX - US_CENTRE) / DEG_MAX;   // 10 µs par degré
}

static void aide()
{
    Serial.println();
    Serial.println("=== Test servos MDB1200WP (pins 2 / 3 / 4) ===");
    Serial.println("  ?      aide");
    Serial.println("  a      balayage auto ON / OFF");
    Serial.println("  m      balayage lent / saut (butee a butee, pire cas DC-DC)");
    Serial.println("  s N    servo : 0 avant, 1 arr. gauche, 2 arr. droit, 3 tous");
    Serial.println("  u N    position en us (900 a 2100)");
    Serial.println("  d N    position en degres (-60 a +60)");
    Serial.println("  c      centre (1500 us)");
    Serial.println("  x      coupe le signal (servos relaches)");
    Serial.println();
}

static void etat()
{
    Serial.printf("[%6lu ms] %s%s | servo choisi : %s |",
                  (unsigned long)millis(),
                  balayage ? "balayage " : "manuel",
                  balayage ? (mode_saut ? "saut" : "lent") : "",
                  choix == 3 ? "tous" : NOMS[choix]);
    for (int i = 0; i < NB_SERVOS; i++) {
        if (actif[i]) Serial.printf("  %s %d us (%+d deg)", NOMS[i], pos_us[i], (pos_us[i] - US_CENTRE) / 10);
        else          Serial.printf("  %s --", NOMS[i]);
    }
    Serial.println();
}

// Exécute une ligne reçue du moniteur série.
static void commande(char* ligne)
{
    while (*ligne == ' ') ligne++;
    char c = ligne[0];
    if (c == 0) return;
    int n = atoi(ligne + 1);

    switch (c) {
    case '?': aide(); return;
    case 'a': balayage = !balayage; break;
    case 'm': mode_saut = !mode_saut; break;
    case 's':
        if (n < 0 || n > 3) { Serial.println("servo : 0, 1, 2 ou 3 (tous)"); return; }
        choix = n; break;
    case 'u':
        if (n < US_MIN || n > US_MAX) Serial.printf("hors plage : borne a %d..%d us\n", US_MIN, US_MAX);
        balayage = false; appliquer(n); break;
    case 'd':
        if (n < -DEG_MAX || n > DEG_MAX) Serial.printf("hors plage : borne a +/-%d deg\n", DEG_MAX);
        balayage = false; appliquer(deg_vers_us(n)); break;
    case 'c': balayage = false; appliquer(US_CENTRE); break;
    case 'x':
        balayage = false;
        for (int i = 0; i < NB_SERVOS; i++) if (choix == 3 || choix == i) couper(i);
        break;
    default:
        Serial.printf("commande inconnue : '%c' (? pour l'aide)\n", c);
        return;
    }
    etat();
}

static void Task_TestServos(void*)
{
    char ligne[32];
    int  n = 0;
    int  sens = 1;                     // balayage lent : +1 monte, -1 descend
    int  cible_saut = US_MAX;
    uint32_t dernier_saut = 0, dernier_affichage = 0;
    TickType_t reveil = xTaskGetTickCount();

    vTaskDelay(pdMS_TO_TICKS(1500));   // laisse le temps d'ouvrir le moniteur
    aide();
    for (int i = 0; i < NB_SERVOS; i++) ecrire_us(i, US_CENTRE);
    etat();
    vTaskDelay(pdMS_TO_TICKS(1000));   // 1 s au centre avant de bouger

    while (1) {
        // 1) Commandes série (non bloquant)
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                ligne[n] = 0;
                if (n) commande(ligne);
                n = 0;
            } else if (n < (int)sizeof(ligne) - 1) {
                ligne[n++] = c;
            }
        }

        // 2) Balayage automatique (les 3 servos ensemble)
        uint32_t now = millis();
        if (balayage) {
            if (mode_saut) {
                if (now - dernier_saut >= (uint32_t)SAUT_PAUSE_MS) {
                    dernier_saut = now;
                    cible_saut = (cible_saut == US_MAX) ? US_MIN : US_MAX;
                    for (int i = 0; i < NB_SERVOS; i++) ecrire_us(i, cible_saut);
                }
            } else {
                int p = pos_us[0] + sens * LENT_PAS_US;
                if (p >= US_MAX) { p = US_MAX; sens = -1; }
                if (p <= US_MIN) { p = US_MIN; sens = +1; }
                for (int i = 0; i < NB_SERVOS; i++) ecrire_us(i, p);
            }
        }

        // 3) État toutes les secondes
        if (now - dernier_affichage >= (uint32_t)AFFICHAGE_MS) {
            dernier_affichage = now;
            etat();
        }

        vTaskDelayUntil(&reveil, pdMS_TO_TICKS(PERIODE_MS));
    }
}

void setup()
{
    Serial.begin(115200);
    analogWriteResolution(PWM_BITS);
    for (int i = 0; i < NB_SERVOS; i++) {
        pinMode(PINS[i], arduino::OUTPUT);
        digitalWrite(PINS[i], 0);
        analogWriteFrequency(PINS[i], PWM_HZ);
    }
    xTaskCreate(Task_TestServos, "Servos", 2048, NULL, 2, NULL);
    vTaskStartScheduler();
}

void loop() {}
