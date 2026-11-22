// =====================================================
//  test_sonars.cpp — test des 3 sonars (IFM UGT207) et calcul de
//  la résistance de mesure de chacun, au ruban à mesurer.
//
//  Programme À PART du firmware : à flasher seul, puis reflasher
//  le firmware normal après le test.
//
//  Même lecture que Task_LectureSonar :
//    ADC 10 bits → tension → courant (résistance SONAR_R_x) →
//    distance (4–20 mA = SONAR_DIST_MIN_CM–SONAR_DIST_MAX_CM),
//    filtre SONAR_ALPHA à 50 Hz, bornée à la plage.
//  Broches et constantes : config.h.
//
//  Affiche toutes les 200 ms les 3 distances :
//    Avant  72.4 cm | Arr. gauche  68.1 cm | Arr. droit  70.3 cm
//
//  CALCUL DES RÉSISTANCES (moniteur série, une commande par ligne) :
//    m S D   mesure le sonar S (0 avant, 1 arr. gauche, 2 arr. droit)
//            avec une planche bien à plat à D cm (ruban), moyenne 3 s
//            ex. « m 0 40 », puis « m 0 120 »
//    r       résumé : lignes à copier dans config.h
//    e       efface les mesures
//    p       pause / reprise de l'affichage des distances
//    ?       aide
//  L'affichage s'arrête dès qu'on commence à taper (ce qu'on tape
//  s'affiche), et reprend après Entrée.
//  Avec 2 distances (au moins 30 cm d'écart) pour un sonar :
//    - résistance effective (Ω) = tension / courant attendu à cette
//      distance, avec l'échelle de config.h ; elle compense aussi
//      l'erreur de référence de l'ADC ;
//    - si les 2 distances ne donnent pas la même résistance (> 2 %),
//      l'échelle 4–20 mA du sonar n'est pas celle de config.h : le
//      programme estime la vraie échelle.
//  Laisser chauffer les sonars 20 min avant (précision de la fiche).
// =====================================================

#include <arduino_freertos.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "config.h"

static const int PERIODE_MS   = PERIODE_SONAR_MS;   // 20 ms, comme le firmware (le filtre en dépend)
static const int AFFICHAGE_MS = 200;
static const int MESURE_MS    = 3000;                // durée d'une mesure au ruban
static const float ECART_D_MIN_CM = 30.0f;           // écart minimum entre les 2 distances
static const float ECART_R_MAX_PCT = 2.0f;           // au-delà : échelle douteuse

static const int   PINS[NB_CANAUX] = {Analog_Sonar_Avant, Analog_Sonar_Arriere_Gauche, Analog_Sonar_Arriere_Droit};
static const char* NOMS[NB_CANAUX] = {"Avant", "Arr. gauche", "Arr. droit"};
static const char* CLES[NB_CANAUX] = {"SONAR_R_AVANT", "SONAR_R_ARRIERE_GAUCHE", "SONAR_R_ARRIERE_DROIT"};
static const float R_CONFIG[NB_CANAUX] = {SONAR_R_AVANT, SONAR_R_ARRIERE_GAUCHE, SONAR_R_ARRIERE_DROIT};

// Points mesurés au ruban : distance (cm) et tension moyenne (V), 2 par sonar
struct Point { float d; float v; bool ok; };
static Point pts[NB_CANAUX][2];
static float r_calc[NB_CANAUX];          // 0 = pas encore calculée
static bool  pause_affichage = false;

static float tension(int pin) { return (analogRead(pin) / SONAR_ADC_BITS) * SONAR_VREF; }

// Distance brute (cm), même conversion que lire_sonar()
static float distance_brute(int i)
{
    float courant_mA = (tension(PINS[i]) / R_CONFIG[i]) * 1000.0f;
    return (courant_mA - SONAR_I_MIN_MA) / (SONAR_I_MAX_MA - SONAR_I_MIN_MA)
           * (SONAR_DIST_MAX_CM - SONAR_DIST_MIN_CM) + SONAR_DIST_MIN_CM;
}

static float borner(float d)
{
    if (d < SONAR_DIST_MIN_CM) return SONAR_DIST_MIN_CM;
    if (d > SONAR_DIST_MAX_CM) return SONAR_DIST_MAX_CM;
    return d;
}

// Courant attendu (mA) à la distance d, avec l'échelle de config.h
static float courant_attendu(float d)
{
    return SONAR_I_MIN_MA + (SONAR_I_MAX_MA - SONAR_I_MIN_MA)
           * (d - SONAR_DIST_MIN_CM) / (SONAR_DIST_MAX_CM - SONAR_DIST_MIN_CM);
}

static void aide()
{
    Serial.println();
    Serial.printf("=== Test sonars (pins %d / %d / %d), %g a %g cm ===\n",
                  PINS[0], PINS[1], PINS[2], (double)SONAR_DIST_MIN_CM, (double)SONAR_DIST_MAX_CM);
    Serial.println("Calcul des resistances : planche a plat, distance au ruban.");
    Serial.println("  m S D   mesure le sonar S (0 avant, 1 arr. gauche, 2 arr. droit) a D cm, 3 s");
    Serial.println("          ex. m 0 40   puis   m 0 120   (au moins 30 cm d'ecart)");
    Serial.println("  r       lignes a copier dans config.h");
    Serial.println("  e       efface les mesures");
    Serial.println("  p       pause / reprise de l'affichage des distances");
    Serial.println("Taper la commande puis Entree (l'affichage s'arrete pendant qu'on tape).");
    Serial.println("  ?       aide");
    Serial.println();
}

// Résistance à partir des 2 points d'un sonar
static void calculer(int i)
{
    Point &a = pts[i][0], &b = pts[i][1];
    if (!a.ok || !b.ok) return;
    if (fabsf(a.d - b.d) < ECART_D_MIN_CM) {
        Serial.printf("  %s : les 2 distances sont trop proches (%.0f et %.0f cm), il faut %g cm d'ecart\n",
                      NOMS[i], (double)a.d, (double)b.d, (double)ECART_D_MIN_CM);
        return;
    }
    float r1 = a.v / courant_attendu(a.d) * 1000.0f;
    float r2 = b.v / courant_attendu(b.d) * 1000.0f;
    float r  = (r1 + r2) / 2.0f;
    float ecart = fabsf(r1 - r2) / r * 100.0f;
    r_calc[i] = r;
    Serial.printf("  %s : R = %.1f ohm  (a %.0f cm : %.1f, a %.0f cm : %.1f, ecart %.1f %%)\n",
                  NOMS[i], (double)r, (double)a.d, (double)r1, (double)b.d, (double)r2, (double)ecart);
    if (ecart > ECART_R_MAX_PCT) {
        // Échelle inconnue : V = R x (4 + k (d - Dmin)), k = 16 / (Dmax - Dmin), 2 inconnues R et k
        float k = 4.0f * (b.v - a.v) / (a.v * (b.d - SONAR_DIST_MIN_CM) - b.v * (a.d - SONAR_DIST_MIN_CM));
        Serial.printf("  ! Les 2 distances ne donnent pas la meme resistance : l'echelle du sonar\n"
                      "    n'est pas %g-%g cm (config.h), ou la planche a bouge / n'etait pas a plat.\n",
                      (double)SONAR_DIST_MIN_CM, (double)SONAR_DIST_MAX_CM);
        if (k > 0.0f) {
            float dmax = SONAR_DIST_MIN_CM + (SONAR_I_MAX_MA - SONAR_I_MIN_MA) / k;
            float rk   = a.v / (SONAR_I_MIN_MA + k * (a.d - SONAR_DIST_MIN_CM)) * 1000.0f;
            Serial.printf("    Estimation si 4 mA = %g cm : 20 mA = %.0f cm, R = %.1f ohm\n",
                          (double)SONAR_DIST_MIN_CM, (double)dmax, (double)rk);
        }
    } else {
        Serial.println("    -> OK : echelle de config.h confirmee");
    }
}

static void resume()
{
    Serial.println();
    Serial.println("// --- a copier dans config.h (section SONARS) ---");
    for (int i = 0; i < NB_CANAUX; i++) {
        if (r_calc[i] > 0.0f)
            Serial.printf("#define %-23s %.1ff\n", CLES[i], (double)r_calc[i]);
        else
            Serial.printf("#define %-23s %.1ff   // pas mesure : valeur actuelle\n", CLES[i], (double)R_CONFIG[i]);
    }
    Serial.println();
}

// Mesure de 3 s : tension moyenne et bruit du sonar i
static void mesurer(int i, float d)
{
    if (d < SONAR_DIST_MIN_CM || d > SONAR_DIST_MAX_CM) {
        Serial.printf("distance hors plage du sonar (%g a %g cm)\n", (double)SONAR_DIST_MIN_CM, (double)SONAR_DIST_MAX_CM);
        return;
    }
    Serial.printf("Mesure %s a %.1f cm pendant %d s, ne pas bouger la planche...\n", NOMS[i], (double)d, MESURE_MS / 1000);
    double s = 0, s2 = 0;
    int n = 0;
    TickType_t reveil = xTaskGetTickCount();
    for (uint32_t t0 = millis(); millis() - t0 < (uint32_t)MESURE_MS; ) {
        float v = tension(PINS[i]);
        s += v; s2 += (double)v * v; n++;
        vTaskDelayUntil(&reveil, pdMS_TO_TICKS(PERIODE_MS));
    }
    float moy = (float)(s / n);
    float bruit = (float)sqrt(fmax(0.0, s2 / n - (s / n) * (s / n)));
    float ma = moy / R_CONFIG[i] * 1000.0f;
    Serial.printf("  %d lectures : %.4f V (bruit %.1f mV), %.2f mA avec %.1f ohm -> %.1f cm lus\n",
                  n, (double)moy, (double)(bruit * 1000.0f), (double)ma, (double)R_CONFIG[i],
                  (double)((ma - SONAR_I_MIN_MA) / (SONAR_I_MAX_MA - SONAR_I_MIN_MA)
                           * (SONAR_DIST_MAX_CM - SONAR_DIST_MIN_CM) + SONAR_DIST_MIN_CM));
    if (ma < SONAR_I_MIN_MA - 0.5f)
        Serial.println("  ! courant sous 4 mA : sonar pas alimente, fil coupe ou planche dans la zone aveugle");
    // Garde les 2 dernières distances différentes
    int k = (pts[i][0].ok && fabsf(pts[i][0].d - d) < 0.5f) ? 0
          : (pts[i][1].ok && fabsf(pts[i][1].d - d) < 0.5f) ? 1
          : (!pts[i][0].ok ? 0 : (!pts[i][1].ok ? 1 : -1));
    if (k < 0) { pts[i][0] = pts[i][1]; k = 1; }
    pts[i][k] = {d, moy, true};
    calculer(i);
}

static void commande(char* ligne)
{
    while (*ligne == ' ') ligne++;
    char* fin;
    switch (ligne[0]) {
    case 0:   return;
    case '?': aide(); return;
    case 'r': resume(); return;
    case 'e':
        memset(pts, 0, sizeof(pts)); memset(r_calc, 0, sizeof(r_calc));
        Serial.println("mesures effacees"); return;
    case 'p':
        pause_affichage = !pause_affichage;
        Serial.println(pause_affichage ? "affichage en pause (p pour reprendre)" : "affichage repris");
        return;
    case 'm': {
        long s = strtol(ligne + 1, &fin, 10);
        float d = strtof(fin, &fin);
        if (s < 0 || s >= NB_CANAUX || d <= 0.0f) {
            Serial.println("usage : m S D   (S = 0 avant, 1 arr. gauche, 2 arr. droit ; D en cm)");
            return;
        }
        mesurer((int)s, d);
        return;
    }
    default:
        Serial.printf("commande inconnue : '%c' (? pour l'aide)\n", ligne[0]);
    }
}

static void Task_TestSonars(void*)
{
    float filt[NB_CANAUX];
    uint32_t dernier_affichage = 0;
    char ligne[32];
    int  n = 0;

    vTaskDelay(pdMS_TO_TICKS(1500));   // laisse le temps d'ouvrir le moniteur
    aide();

    // Le filtre part de la première mesure (pas de rampe au démarrage)
    for (int i = 0; i < NB_CANAUX; i++) filt[i] = borner(distance_brute(i));

    TickType_t reveil = xTaskGetTickCount();
    while (1) {
        // Commandes série. Ce qu'on tape est réaffiché (le moniteur de
        // PlatformIO ne le montre pas) ; l'affichage des distances s'arrête
        // tant que la ligne n'est pas finie. Une mesure le bloque 3 s.
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (n == 0) continue;           // fin de ligne CR+LF : un seul passage
                Serial.println();
                ligne[n] = 0;
                commande(ligne);
                reveil = xTaskGetTickCount();
                n = 0;
            } else if (c == 8 || c == 127) {    // retour arrière
                if (n > 0) { n--; Serial.print("\b \b"); }
            } else if (c >= ' ' && n < (int)sizeof(ligne) - 1) {
                if (n == 0) Serial.print("\n> ");
                ligne[n++] = c;
                Serial.write(c);
            }
        }

        for (int i = 0; i < NB_CANAUX; i++) {
            float d = distance_brute(i);
            filt[i] = borner(SONAR_ALPHA * d + (1.0f - SONAR_ALPHA) * filt[i]);
        }

        uint32_t now = millis();
        if (n == 0 && !pause_affichage && now - dernier_affichage >= (uint32_t)AFFICHAGE_MS) {
            dernier_affichage = now;
            Serial.printf("%s %6.1f cm | %s %6.1f cm | %s %6.1f cm\n",
                          NOMS[0], (double)filt[0], NOMS[1], (double)filt[1], NOMS[2], (double)filt[2]);
        }

        vTaskDelayUntil(&reveil, pdMS_TO_TICKS(PERIODE_MS));
    }
}

void setup()
{
    Serial.begin(115200);
    analogReadResolution(ADC_RESOLUTION);   // 10 bits, comme le firmware
    xTaskCreate(Task_TestSonars, "Sonars", 4096, NULL, 2, NULL);
    vTaskStartScheduler();
}

void loop() {}
