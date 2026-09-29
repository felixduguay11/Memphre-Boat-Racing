#include "Arduino.h"
#include <math.h>

// ═════════════════════════════════════════════════════════════
//  MODE DE FONCTIONNEMENT
// ═════════════════════════════════════════════════════════════
//  1 = SIMULATION PURE   |   0 = MATÉRIEL RÉEL  ← MODE ACTUEL
// ═════════════════════════════════════════════════════════════
#define USE_FAKE_DATA   0

#if !USE_FAKE_DATA
  #include <FlexCAN_T4.h>
  #include <VescCAN_Teensy.h>

  // CAN3 sur Teensy 4.1 = pin 30 (CRX3) / pin 31 (CTX3)
  FlexCAN_T4<CAN3, RX_SIZE_256, TX_SIZE_16> rawCan1;
  FlexCANAdapter<CAN3> adapterCan1(rawCan1);
  VescCANBus vesc(adapterCan1);

  #define BAUD_RATE   250000   // VESC : 250 kbps par défaut
#endif

#define VESC_ID_A   10
#define VESC_ID_B   11
#define PIN_LEVIER_VITESSE 27
#define PIN_SWITCH_IN 38
#define PIN_SWITCH_F_R 40
#define RAMP_STEP 20         // Changement max d'ERPM par cycle (~20ms)

#define VESC_TIMEOUT_MS  500

// ─────────────────────────────────────────────────────────────
//  SÉCURITÉ — logique des entrées
// ─────────────────────────────────────────────────────────────
//  Les deux switchs sont câblés en PULL-UP : le switch fermé tire
//  la pin à la masse. Débranché ou fil coupé = la résistance de
//  tirage ramène à 3,3 V = HIGH.
//  Règle : HIGH (fil arraché, switch ouvert) doit TOUJOURS donner
//  l'état le plus sûr. Donc RUN demande explicitement un LOW.
#define SWITCH_ACTIF        LOW

//  Levier : bornes réelles MESURÉES de l'ADC (10 bits).
//  Relevé au banc : repos = 230, butée = 730.
//  La course mécanique du housing ne couvre que ~half de la
//  plage 0-1023, d'où ces valeurs.
#define LEVIER_RAW_MIN      230    // valeur au repos (levier relâché)
#define LEVIER_RAW_MAX      730    // valeur à fond
#define LEVIER_DEADBAND     30     // sous ce delta au-dessus du MIN -> 0

//  Armement : tant que le levier n'a pas été vu au neutre une fois
//  depuis le démarrage (ou depuis un retour en IDLE), aucune
//  consigne n'est envoyée. Empêche le démarrage moteur sur une
//  entrée flottante ou un levier laissé poussé au contact.
//
//  ARMEMENT_REQUIS  1 = protection active (recommandé sur l'eau)
//                   0 = désactivée, le levier répond tout de suite
#define ARMEMENT_REQUIS     1

//  Seuil d'armement : raw en dessous duquel le levier est considéré
//  "au neutre" pour armer. Plus permissif que le deadband, sinon un
//  levier qui ne redescend pas tout à fait n'arme jamais.
//  Mettre plus haut si l'armement ne se fait pas (ex. 320).
#define LEVIER_RAW_ARME     300

bool levierArme = false;

// ─────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────
//  HÉLICES CONTRAROTATIVES
// ─────────────────────────────────────────────────────────────
//  Les deux hélices sont en miroir (une à pas droit, une à pas
//  gauche) : elles doivent tourner en sens OPPOSÉS pour pousser
//  dans la même direction.
//
//  Si le bateau recule quand le levier demande l'avant, inverse
//  les DEUX lignes ci-dessous (1 <-> 0). Ne jamais mettre les
//  deux à la même valeur : les hélices se combattraient.
#define VESC_A_INVERSE   0
#define VESC_B_INVERSE   1

// ─────────────────────────────────────────────────────────────
//  Bornes de consigne (eRPM) — plage UTILE du moteur
// ─────────────────────────────────────────────────────────────
//  Sous ERPM_MIN_UTILE le moteur ne tourne pas proprement, donc
//  le levier saute directement à cette valeur dès qu'il sort du
//  deadband, puis monte linéairement jusqu'au max.
//  Levier au repos = 0 eRPM franc (moteur arrêté), jamais 1800.
#define ERPM_MIN_UTILE     1800
#define ERPM_MAX_FORWARD   5500
#define ERPM_MAX_REVERSE   3000   // magnitude ; le signe est mis dans le code

// ─────────────────────────────────────────────────────────────
//  LIMITE DE TEMPÉRATURE — mêmes valeurs que config.py côté Pi
// ─────────────────────────────────────────────────────────────
//  Dès qu'une température (FET ou moteur, d'un des deux VESC)
//  atteint sa limite, la consigne est plafonnée à
//  ERPM_MAX_SURCHAUFFE. La rampe normale fait redescendre le
//  moteur en douceur : pas de coupure nette, le bateau garde de
//  la poussée. Le pilote voit NOT OK sur l'écran au même moment.
//
//  Retour à la normale seulement quand TOUTES les températures
//  sont repassées sous (limite - TEMP_HYST).
//
//  Si tu changes ces valeurs, change aussi T_FET_MAX, T_MOT_MAX
//  et TEMP_HYST dans config.py, sinon l'écran et le Teensy ne
//  seront pas d'accord.
#define LIMITE_TEMP_ACTIVE   1        // 0 = surveillance désactivée
#define T_FET_MAX            75.0f    // °C
#define T_MOT_MAX            75.0f    // °C
#define TEMP_HYST            3.0f     // °C
#define ERPM_MAX_SURCHAUFFE  3000     // plafond de consigne en surchauffe

static_assert(ERPM_MAX_SURCHAUFFE >= ERPM_MIN_UTILE,
              "le plafond de surchauffe doit rester dans la plage utile");

bool surchauffe = false;

// ─────────────────────────────────────────────────────────────
//  LIAISON RASPBERRY PI  (USB = Serial = /dev/ttyACM0)
// ─────────────────────────────────────────────────────────────
#define DEBUG_PRINT     0     // texte lisible en plus du JSON (app fermée)
#define DEBUG_LEVIER    0     // 1 = imprime le raw ADC pour calibrer
#define PRINT_MS        1000
#define TLM_MS          50    // télémétrie : 20 Hz, si streaming
#define HB_MS           500   // battement de cœur sinon
#define PI_LINE_MAX     96

bool     streaming   = false;
uint32_t lastTlmMs   = 0;
uint32_t lastBeatMs  = 0;
uint32_t lastPrintMs = 0;
char     piLine[PI_LINE_MAX];
uint8_t  piIdx       = 0;

enum ControlMode { IDLE, RUN };
enum RunMode     { FORWARD, REVERSE, NEUTRAL };

ControlMode currentMode    = IDLE;
RunMode     currentRunMode = NEUTRAL;
uint32_t lastSendRef = 0;   // millis() du dernier envoi aux VESC
int   target       = 0;
float rampedTarget = 0;

int  StateMachine(int target);
int  LireLevier();              // 0..1023 nettoyé, 0 si sous le deadband
int  CreateTargetForward();
int  CreateTargetReverse();
void readPiCommands();
void handlePiCommand(const char* line);
void sendTelemetry();
void sendHeartbeat();
void printEscJson(int id);
bool vescVivant(int id);
void MajSurchauffe();
// void printFakeEscJson(int id, float phase);   // ← SIMULATION

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
  Serial.begin(115200);       // USB CDC : le débit est ignoré

#if USE_FAKE_DATA
  // ---------- SIMULATION : aucun périphérique initialisé ----------
  // randomSeed(micros());

#else
  // ---------- RÉEL : switchs, levier, CAN, VESC ----------
  pinMode(PIN_SWITCH_IN,  INPUT_PULLUP);
  pinMode(PIN_SWITCH_F_R, INPUT_PULLUP);

  analogReadResolution(10);   // le map() du levier suppose 0..1023

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

  // ── 2. Switchs + levier ────────────────────────────────────
  //  RUN exige un LOW franc sur PIN_SWITCH_IN (switch fermé).
  //  Tout le reste (fil débranché, switch ouvert) = IDLE.
  if (digitalRead(PIN_SWITCH_IN) != SWITCH_ACTIF) {
    currentMode    = IDLE;
    currentRunMode = NEUTRAL;
    target         = 0;
    levierArme     = false;      // il faudra repasser par le neutre
  } else {
    currentMode = RUN;

    // Armement : on refuse toute consigne tant que le levier
    // n'a pas été vu au neutre au moins une fois.
    if (ARMEMENT_REQUIS && !levierArme) {
      if (analogRead(PIN_LEVIER_VITESSE) <= LEVIER_RAW_ARME) levierArme = true;
      currentRunMode = NEUTRAL;
      target         = 0;
    }
    else if (digitalRead(PIN_SWITCH_F_R) == SWITCH_ACTIF) {
      currentRunMode = REVERSE;
      target = CreateTargetReverse();
    } else {
      currentRunMode = FORWARD;
      target = CreateTargetForward();
    }

    // Levier au repos : on affiche NEUTRAL plutôt que FORWARD à 0.
    if (target == 0) currentRunMode = NEUTRAL;
  }

  // ── 2b. Limite de température ─────────────────────────────
  MajSurchauffe();
  if (LIMITE_TEMP_ACTIVE && surchauffe) {
    target = constrain(target, -ERPM_MAX_SURCHAUFFE, ERPM_MAX_SURCHAUFFE);
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
    Serial.printf("[dbg] mode=%s run=%s arme=%d chaud=%d target=%d ramped=%.0f\n",
                  modeStr(), runStr(), levierArme ? 1 : 0, surchauffe ? 1 : 0,
                  target, rampedTarget);
  }
#endif

#if DEBUG_LEVIER && !USE_FAKE_DATA
  // Calibration du levier : app Pi FERMÉE, screen /dev/ttyACM0.
  // Note la valeur au repos -> LEVIER_RAW_MIN
  // Note la valeur à fond   -> LEVIER_RAW_MAX
  if (millis() - lastPrintMs >= 200) {
    lastPrintMs = millis();
    Serial.printf("[levier] raw=%d nettoye=%d\n",
                  analogRead(PIN_LEVIER_VITESSE), LireLevier());
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
    // NOTE : "stop" arrête seulement la télémétrie, pas les moteurs.
    // Pour que le bouton Stop de l'UI coupe aussi la consigne
    // (retour au neutre obligatoire ensuite), décommenter :
    // levierArme = false;
  }
}


// ─────────────────────────────────────────────────────────────
//  Émission de la télémétrie — format contractuel avec l'UI
// ─────────────────────────────────────────────────────────────
void sendTelemetry() {
#if USE_FAKE_DATA
  // ================= SIMULATION (désactivée) =================
  // static uint32_t n = 0;
  // n++;
  // float phase = (float)n;
  // float erpm  = 3200.0f + sinf(phase / 25.0f) * 450.0f;
  //
  // Serial.printf("{\"t\":%lu,\"mode\":\"RUN\",\"run\":\"FORWARD\","
  //               "\"target\":%ld,\"ramped\":%ld,\"esc\":[",
  //               (unsigned long)millis(),
  //               (long)erpm, (long)(erpm * 0.99f));
  // printFakeEscJson(VESC_ID_A, phase);
  // Serial.print(',');
  // printFakeEscJson(VESC_ID_B, phase * 0.98f);
  // Serial.println("]}");
  // ===========================================================

#else
  Serial.printf("{\"t\":%lu,\"mode\":\"%s\",\"run\":\"%s\","
                "\"target\":%ld,\"ramped\":%ld,\"hot\":%d,\"esc\":[",
                (unsigned long)millis(), modeStr(), runStr(),
                (long)target, (long)rampedTarget, surchauffe ? 1 : 0);
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


// ═════════════════════════════════════════════════════════════
//  SIMULATION — conservée pour retester sans matériel.
//  Remettre USE_FAKE_DATA à 1, décommenter ce bloc, l'appel dans
//  sendTelemetry() et le prototype en haut du fichier.
// ═════════════════════════════════════════════════════════════
// void printFakeEscJson(int id, float phase) {
//   float bruit = (float)random(-200, 201) / 1000.0f;
//   float erpm  = 3200.0f + sinf(phase / 25.0f) * 450.0f;
//   float vin   = 71.0f + sinf(phase / 40.0f) * 1.4f;
//   float iin   = 9.0f  + sinf(phase / 15.0f) * 2.5f + bruit;
//
//   Serial.printf("{\"id\":%d,\"ok\":1,\"erpm\":%ld,\"duty\":%.3f,"
//                 "\"i_mot\":%.2f,\"i_in\":%.2f,\"v_in\":%.2f,"
//                 "\"t_fet\":%.1f,\"t_mot\":%.1f}",
//                 id, (long)erpm,
//                 0.42f + sinf(phase / 25.0f) * 0.05f,
//                 iin + 1.0f, iin, vin + bruit / 10.0f,
//                 42.0f + phase / 4000.0f + bruit,
//                 38.0f + phase / 5000.0f + bruit);
// }


#if !USE_FAKE_DATA
// ═════════════════════════════════════════════════════════════
//  CE QUI SUIT NE COMPILE QU'EN MODE RÉEL
// ═════════════════════════════════════════════════════════════

bool vescVivant(int id) {
  return vesc.isUpdated(id) &&
         (millis() - vesc.lastUpdateMs(id) < VESC_TIMEOUT_MS);
}

// Met a jour `surchauffe` avec hysteresis, VESC par VESC.
// Un VESC muet garde son dernier etat : ses temperatures sont
// perimees, on ne peut ni declencher ni lever l'alerte sur lui.
// Un VESC chaud qui decroche du CAN laisse donc la limite active.
void MajSurchauffe() {
  static bool chaudA = false, chaudB = false;
  const int ids[]    = { VESC_ID_A, VESC_ID_B };
  bool*     chauds[] = { &chaudA,   &chaudB   };

  for (int k = 0; k < 2; k++) {
    int id = ids[k];
    if (!vescVivant(id)) continue;
    float tf = vesc.getTempFET(id);
    float tm = vesc.getTempMotor(id);
    if (tf >= T_FET_MAX || tm >= T_MOT_MAX) {
      *chauds[k] = true;
    } else if (tf < T_FET_MAX - TEMP_HYST && tm < T_MOT_MAX - TEMP_HYST) {
      *chauds[k] = false;
    }
    // entre les deux : on garde l'etat precedent (hysteresis)
  }
  surchauffe = chaudA || chaudB;
}

void printEscJson(int id) {
  bool vivant = vescVivant(id);

  Serial.printf("{\"id\":%d,\"ok\":%d,\"erpm\":%ld,\"duty\":%.3f,"
                "\"i_mot\":%.2f,\"i_in\":%.2f,\"v_in\":%.2f,"
                "\"t_fet\":%.1f,\"t_mot\":%.1f}",
                id,
                vivant ? 1 : 0,
                (long)vesc.getERPM(id),
                vesc.getDutyCycle(id),
                vesc.getMotorCurrent(id),
                vesc.getCurrentIn(id),
                vesc.getVoltageIn(id),
                vesc.getTempFET(id),
                vesc.getTempMotor(id));
}

int StateMachine(int target) {
  if (millis() - lastSendRef >= 20) {
    lastSendRef = millis();

    if (target == 0) {
      // Arret : coupure nette. En mode vitesse, une consigne sous le
      // Minimum ERPM du VESC laisse le moteur en roue libre (pas de frein).
      rampedTarget = 0;
    } else {
      bool memeSens = (rampedTarget > 0) == (target > 0);

      // Depart a l'arret, ou inversion une fois redescendu au minimum :
      // on saute directement a ERPM_MIN_UTILE au lieu de traverser
      // la zone 0..1800 ou le moteur tourne mal.
      if (rampedTarget == 0 ||
          (!memeSens && fabsf(rampedTarget) <= ERPM_MIN_UTILE)) {
        rampedTarget = (target > 0) ? ERPM_MIN_UTILE : -ERPM_MIN_UTILE;
      }

      // Rampe normale au-dessus du minimum (et descente progressive
      // avant une inversion de sens, pour menager les helices).
      if (rampedTarget < target) {
        rampedTarget = min(rampedTarget + RAMP_STEP, (float)target);
      } else if (rampedTarget > target) {
        rampedTarget = max(rampedTarget - RAMP_STEP, (float)target);
      }
    }

    vesc.setERPM(VESC_ID_A,
                 (int32_t)(VESC_A_INVERSE ? -rampedTarget : rampedTarget));
    vesc.setERPM(VESC_ID_B,
                 (int32_t)(VESC_B_INVERSE ? -rampedTarget : rampedTarget));
  }
  return (int)rampedTarget;
}

// Levier nettoyé : renvoie 0..1023 recalé sur les bornes réelles,
// et 0 franc tant qu'on est dans le deadband du repos.
// Une entrée flottante (pin non branchée) reste bruyante : c'est
// l'armement, pas le deadband, qui protège de ce cas.
int LireLevier() {
  int raw = analogRead(PIN_LEVIER_VITESSE);

  if (raw <= LEVIER_RAW_MIN + LEVIER_DEADBAND) return 0;
  if (raw >= LEVIER_RAW_MAX) return 1023;

  return map(raw, LEVIER_RAW_MIN + LEVIER_DEADBAND, LEVIER_RAW_MAX, 0, 1023);
}

int CreateTargetForward(){
  int lev = LireLevier();
  if (lev == 0) return 0;                 // repos : moteur arrêté
  return map(lev, 1, 1023, ERPM_MIN_UTILE, ERPM_MAX_FORWARD);
}

int CreateTargetReverse(){
  int lev = LireLevier();
  if (lev == 0) return 0;                 // repos : moteur arrêté
  return -map(lev, 1, 1023, ERPM_MIN_UTILE, ERPM_MAX_REVERSE);
}

#else
// Souches vides : le projet compile en simulation sans matériel.
void printEscJson(int)     { }
bool vescVivant(int)       { return false; }
void MajSurchauffe()       { }
int  StateMachine(int)     { return 0; }
int  LireLevier()          { return 0; }
int  CreateTargetForward() { return 0; }
int  CreateTargetReverse() { return 0; }
#endif
