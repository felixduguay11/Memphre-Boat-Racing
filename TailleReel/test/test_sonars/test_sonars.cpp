// =====================================================
//  test_sonars.cpp — test unitaire des 3 sonars (IFM UGT207)
//
//  Programme À PART du firmware : environnement PlatformIO
//  « test_sonars » (platformio.ini). Le firmware normal
//  (env teensy41) ne compile pas ce dossier.
//
//    Compiler + flasher : pio run -e test_sonars -t upload
//    Moniteur série     : pio device monitor -e test_sonars
//
//  Même lecture que Task_LectureSonar :
//    ADC 10 bits → tension → courant (résistance 150 Ω) →
//    distance (4–20 mA = SONAR_DIST_MIN_CM–SONAR_DIST_MAX_CM),
//    filtre SONAR_ALPHA à 50 Hz, bornée à la plage.
//  Broches et constantes : config.h (pins 14 / 15 / 16).
//
//  Affiche une ligne toutes les 200 ms :
//    Avant  72.4 cm | Arr. gauche  68.1 cm | Arr. droit  70.3 cm
// =====================================================

#include <arduino_freertos.h>
#include "config.h"

static const int PERIODE_MS   = PERIODE_SONAR_MS;   // 20 ms, comme le firmware (le filtre en dépend)
static const int AFFICHAGE_MS = 200;

static const int   PINS[NB_CANAUX] = {Analog_Sonar_Avant, Analog_Sonar_Arriere_Gauche, Analog_Sonar_Arriere_Droit};
static const char* NOMS[NB_CANAUX] = {"Avant", "Arr. gauche", "Arr. droit"};

// Distance brute (cm) d'un sonar, même conversion que lire_sonar()
static float distance_brute(int pin)
{
    int raw = analogRead(pin);
    float voltage    = (raw / SONAR_ADC_BITS) * SONAR_VREF;
    float courant_mA = (voltage / SONAR_RESISTANCE) * 1000.0f;
    return (courant_mA - SONAR_I_MIN_MA) / (SONAR_I_MAX_MA - SONAR_I_MIN_MA)
           * (SONAR_DIST_MAX_CM - SONAR_DIST_MIN_CM) + SONAR_DIST_MIN_CM;
}

static float borner(float d)
{
    if (d < SONAR_DIST_MIN_CM) return SONAR_DIST_MIN_CM;
    if (d > SONAR_DIST_MAX_CM) return SONAR_DIST_MAX_CM;
    return d;
}

static void Task_TestSonars(void*)
{
    float filt[NB_CANAUX];
    uint32_t dernier_affichage = 0;

    vTaskDelay(pdMS_TO_TICKS(1500));   // laisse le temps d'ouvrir le moniteur
    Serial.println();
    Serial.printf("=== Test sonars (pins %d / %d / %d), %g a %g cm ===\n",
                  PINS[0], PINS[1], PINS[2], (double)SONAR_DIST_MIN_CM, (double)SONAR_DIST_MAX_CM);

    // Le filtre part de la première mesure (pas de rampe au démarrage)
    for (int i = 0; i < NB_CANAUX; i++) filt[i] = borner(distance_brute(PINS[i]));

    TickType_t reveil = xTaskGetTickCount();
    while (1) {
        for (int i = 0; i < NB_CANAUX; i++) {
            float d = distance_brute(PINS[i]);
            filt[i] = borner(SONAR_ALPHA * d + (1.0f - SONAR_ALPHA) * filt[i]);
        }

        uint32_t now = millis();
        if (now - dernier_affichage >= (uint32_t)AFFICHAGE_MS) {
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
    xTaskCreate(Task_TestSonars, "Sonars", 2048, NULL, 2, NULL);
    vTaskStartScheduler();
}

void loop() {}
