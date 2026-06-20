#include "Arduino.h"
#include <FlexCAN_T4.h>
#include <VescCAN_Teensy.h>

FlexCAN_T4<CAN3, RX_SIZE_256, TX_SIZE_16> rawCan1;
FlexCANAdapter<CAN3> adapterCan1(rawCan1);
VescCANBus vesc(adapterCan1);

#define BAUD_RATE   250000   // VESC utilise 250 kbps par défaut
#define COMM_RATE   50      // Intervalle d'envoi de commandes (ms)
#define VESC_ID_A   10       // ID du premier ESC  (régler dans VESC Tool)
#define VESC_ID_B   11       // ID du deuxième ESC (si présent)
#define PIN_LEVIER_VITESSE 27
#define PIN_SWITCH_IN 38
#define PIN_SWITCH_F_R 40
#define RAMP_STEP 20        // Changement max d'ERPM par cycle StateMachine (~20ms) -> limite l'accélération, évite un saut brusque forward/reverse. À ajuster selon ton application.

enum ControlMode {
  IDLE,
  RUN
};

enum RunMode {
  FORWARD,
  REVERSE,
  NEUTRAL
};
  
ControlMode currentMode = IDLE;
RunMode currentRunMode = NEUTRAL;
float lastSendRef = 0;
int target = 0;
float rampedTarget = 0;   // valeur réellement envoyée, lissée vers "target"
unsigned long lastSendA = 0;
unsigned long lastSendB = 0;
void StateMachine(int target);
int CreateTargetForward();
int CreateTargetReverse();


void setup() {
  Serial.begin(9600);       // debug USB
  pinMode(PIN_SWITCH_IN, INPUT_PULLUP);
  pinMode(PIN_SWITCH_F_R, INPUT_PULLUP);

  while (!Serial && millis() < 3000);

  rawCan1.begin();            // Initialisation bus CAN 1
  rawCan1.setBaudRate(BAUD_RATE);
  vesc.begin();               // initialise les structures internes

  Serial.println("=== VESC CAN Teensy 4.1 — Prêt ===");
  Serial.printf("CAN1 @ %u bps | ESCs surveillés : %u, %u\n\n",
                BAUD_RATE, VESC_ID_A, VESC_ID_B);
}

void loop() {
  // ── 1. Mise à jour lecture CAN ────────────────────────────────
  vesc.update();   // draine tous les messages en attente

  // ── 2. Lecture des switchs à chaque passage (plus de while bloquant) ──
  if (digitalRead(PIN_SWITCH_IN) == HIGH) {
    currentMode = IDLE;
    currentRunMode = NEUTRAL;
    target = 0;
  } else {
    currentMode = RUN;
    if (digitalRead(PIN_SWITCH_F_R) == LOW) {
      currentRunMode = FORWARD;
      target = CreateTargetForward();
    } else {
      currentRunMode = REVERSE;
      target = CreateTargetReverse();
    }
  }

  StateMachine(target);
}


void StateMachine(int target) {
  if (millis() - lastSendRef > 20) {

    if (rampedTarget < target) {
      rampedTarget = min(rampedTarget + RAMP_STEP, (float)target);
    } else if (rampedTarget > target) {
      rampedTarget = max(rampedTarget - RAMP_STEP, (float)target);
    }

    switch (currentMode) {
      case IDLE:
        vesc.setERPM(VESC_ID_A, rampedTarget);
        vesc.setERPM(VESC_ID_B, rampedTarget);
        Serial.print("IDLE    ");
        Serial.println(rampedTarget);
        break;
      case RUN:
        switch(currentRunMode){
          case NEUTRAL:
            vesc.setERPM(VESC_ID_A, rampedTarget);
            vesc.setERPM(VESC_ID_B, rampedTarget);
            Serial.print("NEUTRAL");
            break;
          case FORWARD:
            vesc.setERPM(VESC_ID_A, rampedTarget);
            vesc.setERPM(VESC_ID_B, rampedTarget);
            Serial.print("FORWARD    ");
            Serial.println(rampedTarget);
            break;
          case REVERSE:
            vesc.setERPM(VESC_ID_A, rampedTarget);
            vesc.setERPM(VESC_ID_B, rampedTarget);
            Serial.print("REVERSE    ");
            Serial.println(rampedTarget);
            break;
        }
    }
    lastSendRef = millis();
  }
}

int CreateTargetForward(){
  int raw = analogRead(PIN_LEVIER_VITESSE);
  //raw = constrain(raw, 100, 1023);   // évite l'extrapolation de map() hors plage
  int desired_speed = map(raw, 0, 1023, 0, 8000);
  return desired_speed;
}

int CreateTargetReverse(){
  int raw = analogRead(PIN_LEVIER_VITESSE);
  //raw = constrain(raw, 100, 1023);   // évite l'extrapolation de map() hors plage
  int desired_speed = map(raw, 0, 1023, 0, -3000);
  return desired_speed;
}