

#include "Arduino.h"
#include <math.h>

// ═════════════════════════════════════════════════════════════
//  MODE DE FONCTIONNEMENT
// ═════════════════════════════════════════════════════════════
//  1 = SIMULATION PURE
//      Aucun capteur lu, aucun bus CAN, aucun moteur commandé.
//      Le Teensy ne fait que parler JSON au Pi.
//      Sûr à flasher avec rien de branché.
//
//  0 = MATÉRIEL RÉEL
//      Réactive le CAN, les switchs, le levier et les VESC.
// ═════════════════════════════════════════════════════════════
#define USE_FAKE_DATA   1

#if !USE_FAKE_DATA
  #include <FlexCAN_T4.h>
  #include <VescCAN_Teensy.h>

  FlexCAN_T4<CAN3, RX_SIZE_256, TX_SIZE_16> rawCan1;
  FlexCANAdapter<CAN3> adapterCan1(rawCan1);
  VescCANBus vesc(adapterCan1);

  #define BAUD_RATE   250000   // VESC utilise 250 kbps par défaut
#endif

#define VESC_ID_A   10       // ID du premier ESC  (régler dans VESC Tool)
#define VESC_ID_B   11       // ID du deuxième ESC
#define PIN_LEVIER_VITESSE 27
#define PIN_SWITCH_IN 38
#define PIN_SWITCH_F_R 40
#define RAMP_STEP 20        // Changement max d'ERPM par cycle (~20ms)

// ─────────────────────────────────────────────────────────────
//  LIAISON RASPBERRY PI  (USB = Serial)
// ─────────────────────────────────────────────────────────────
#define DEBUG_PRINT     0     // prints lisibles en plus du JSON (sans UI)
#define PRINT_MS        1000
#define TLM_MS          50    // télémétrie : 20 Hz, si streaming
#define HB_MS           500   // battement de cœur sinon
#define PI_LINE_MAX     96

bool     streaming   = false;   // passe à true sur {"cmd":"start"}
uint32_t lastTlmMs   = 0;
uint32_t lastBeatMs  = 0;
uint32_t lastPrintMs = 0;
char     piLine[PI_LINE_MAX];
uint8_t  piIdx       = 0;

enum ControlMode { IDLE, RUN };
enum RunMode     { FORWARD, REVERSE, NEUTRAL };

ControlMode currentMode    = IDLE;
RunMode     currentRunMode = NEUTRAL;
float lastSendRef  = 0;
int   target       = 0;
float rampedTarget = 0;   // valeur réellement envoyée, lissée vers "target"

int  StateMachine(int target);
int  CreateTargetForward();
int  CreateTargetReverse();
void readPiCommands();
void handlePiCommand(const char* line);
void sendTelemetry();
void sendHeartbeat();
void printEscJson(int id);
void printFakeEscJson(int id, float phase);

const char* modeStr() { return (currentMode == RUN) ? "RUN" : "IDLE"; }
const char* runStr()  {
  switch (currentRunMode) {
    case FORWARD: return "FORWARD";
    case REVERSE: return "REVERSE";
    default:      return "NEUTRAL";
  }
}


// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);       // USB CDC : le débit est ignoré, c'est normal

#if USE_FAKE_DATA
  // --- SIMULATION : aucun périphérique initialisé ---
  randomSeed(micros());

#else
  // --- RÉEL : switchs, CAN, VESC ---
  pinMode(PIN_SWITCH_IN,  INPUT_PULLDOWN);
  pinMode(PIN_SWITCH_F_R, INPUT_PULLDOWN);

  rawCan1.begin();
  rawCan1.setBaudRate(BAUD_RATE);
  vesc.begin();
#endif
}


// ═════════════════════════════════════════════════════════════
void loop() {

#if !USE_FAKE_DATA
  // ── 1. Lecture CAN — toujours, sinon le buffer déborde ──────
  vesc.update();

  // ── 2. Lecture des switchs et du levier ────────────────────
  if (digitalRead(PIN_SWITCH_IN) == HIGH) {
    currentMode    = IDLE;
    currentRunMode = NEUTRAL;
    target         = 0;
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

  // ── 3. Moteurs — indépendant du streaming ──────────────────
  //     Si le Pi plante, le bateau répond quand même au levier.
  StateMachine(target);
#endif

  // ── 4. Commandes du Pi — non bloquant ──────────────────────
  readPiCommands();

  // ── 5. Sortie vers le Pi ───────────────────────────────────
  if (streaming) {
    if (millis() - lastTlmMs >= TLM_MS) {
      lastTlmMs = millis();
      sendTelemetry();
    }
  } else {
    if (millis() - lastBeatMs >= HB_MS) {
      lastBeatMs = millis();
      sendHeartbeat();
    }
  }

#if DEBUG_PRINT && !USE_FAKE_DATA
  if (millis() - lastPrintMs >= PRINT_MS) {
    lastPrintMs = millis();
    Serial.printf("[dbg] mode=%s run=%s target=%d ramped=%.0f\n",
                  modeStr(), runStr(), target, rampedTarget);
  }
#endif
}


// ─────────────────────────────────────────────────────────────
//  Lecture non bloquante des commandes du Pi
// ─────────────────────────────────────────────────────────────
void readPiCommands() {
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

// Parsing volontairement minimal : on cherche la valeur de "cmd".
void handlePiCommand(const char* line) {
  const char* p = strstr(line, "\"cmd\"");
  if (!p) return;

  if (strstr(p, "start")) {
    streaming  = true;
    lastTlmMs  = millis();
    // Les gains PID arriveront ici plus tard (clé "pid").
  }
  else if (strstr(p, "stop")) {
    streaming  = false;
    lastBeatMs = millis();
  }
}


// ─────────────────────────────────────────────────────────────
//  Émission de la télémétrie — format contractuel avec l'UI
// ─────────────────────────────────────────────────────────────
void sendTelemetry() {
#if USE_FAKE_DATA
  static uint32_t n = 0;
  n++;
  float phase = (float)n;
  float erpm  = 3200.0f + sinf(phase / 25.0f) * 450.0f;

  Serial.printf("{\"t\":%lu,\"mode\":\"RUN\",\"run\":\"FORWARD\","
                "\"target\":%ld,\"ramped\":%ld,\"esc\":[",
                (unsigned long)millis(),
                (long)erpm, (long)(erpm * 0.99f));
  printFakeEscJson(VESC_ID_A, phase);
  Serial.print(',');
  printFakeEscJson(VESC_ID_B, phase * 0.98f);
  Serial.println("]}");

#else
  Serial.printf("{\"t\":%lu,\"mode\":\"%s\",\"run\":\"%s\","
                "\"target\":%ld,\"ramped\":%ld,\"esc\":[",
                (unsigned long)millis(), modeStr(), runStr(),
                (long)target, (long)rampedTarget);
  printEscJson(VESC_ID_A);
  Serial.print(',');
  printEscJson(VESC_ID_B);
  Serial.println("]}");
#endif
}

void sendHeartbeat() {
  Serial.printf("{\"type\":\"hb\",\"state\":\"idle\",\"t\":%lu}\n",
                (unsigned long)millis());
}

// Un ESC, valeurs simulées
void printFakeEscJson(int id, float phase) {
  float bruit = (float)random(-200, 201) / 1000.0f;
  float erpm  = 3200.0f + sinf(phase / 25.0f) * 450.0f;
  float vin   = 71.0f + sinf(phase / 40.0f) * 1.4f;
  float iin   = 9.0f  + sinf(phase / 15.0f) * 2.5f + bruit;

  Serial.printf("{\"id\":%d,\"ok\":1,\"erpm\":%ld,\"duty\":%.3f,"
                "\"i_mot\":%.2f,\"i_in\":%.2f,\"v_in\":%.2f,"
                "\"t_fet\":%.1f,\"t_mot\":%.1f}",
                id,
                (long)erpm,
                0.42f + sinf(phase / 25.0f) * 0.05f,
                iin + 1.0f,
                iin,
                vin + bruit / 10.0f,
                42.0f + phase / 4000.0f + bruit,
                38.0f + phase / 5000.0f + bruit);
}


#if !USE_FAKE_DATA
// ═════════════════════════════════════════════════════════════
//  CE QUI SUIT NE COMPILE QU'EN MODE RÉEL
// ═════════════════════════════════════════════════════════════

// Un ESC, vraies valeurs
void printEscJson(int id) {
  Serial.printf("{\"id\":%d,\"ok\":%d,\"erpm\":%ld,\"duty\":%.3f,"
                "\"i_mot\":%.2f,\"i_in\":%.2f,\"v_in\":%.2f,"
                "\"t_fet\":%.1f,\"t_mot\":%.1f}",
                id,
                vesc.isUpdated(id) ? 1 : 0,
                (long)vesc.getERPM(id),
                vesc.getDutyCycle(id),
                vesc.getMotorCurrent(id),
                vesc.getCurrentIn(id),
                vesc.getVoltageIn(id),
                vesc.getTempFET(id),
                vesc.getTempMotor(id));
}

int StateMachine(int target) {
  if (millis() - lastSendRef > 20) {

    if (rampedTarget < target) {
      rampedTarget = min(rampedTarget + RAMP_STEP, (float)target);
    } else if (rampedTarget > target) {
      rampedTarget = max(rampedTarget - RAMP_STEP, (float)target);
    }

    vesc.setERPM(VESC_ID_A, rampedTarget);
    vesc.setERPM(VESC_ID_B, rampedTarget);

    lastSendRef = millis();
  }
  return rampedTarget;
}

int CreateTargetForward(){
  int raw = analogRead(PIN_LEVIER_VITESSE);
  return map(raw, 0, 1023, 0, 8000);
}

int CreateTargetReverse(){
  int raw = analogRead(PIN_LEVIER_VITESSE);
  return map(raw, 0, 1023, 0, -3000);
}

#else
// Souches vides : le projet compile en simulation sans matériel.
void printEscJson(int)     { }
int  StateMachine(int)     { return 0; }
int  CreateTargetForward() { return 0; }
int  CreateTargetReverse() { return 0; }
#endif