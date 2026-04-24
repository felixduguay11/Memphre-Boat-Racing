/*
 * vesc_can_reader_v4.ino
 * Programme principal — remplace vesc_can_reader_v3.ino
 *
 * Démontre :
 *  - Lecture temps réel de plusieurs ESCs (IDs 10 et 11 ici)
 *  - Envoi de commandes moteur
 *  - Deux bus CAN (CAN1 et CAN2) indépendants si nécessaire
 *
 * Matériel : Teensy 4.1
 * Librairie : FlexCAN_T4 (déjà présente dans votre projet)
 */

#include <FlexCAN_T4.h>
#include "VescCAN_Teensy.h"

// ──────────────────────────────────────────────────────────────
//  Déclaration des bus CAN physiques
//  Adaptez CAN1 / CAN2 aon votre câblage.
// ──────────────────────────────────────────────────────────────
FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> rawCan1;
FlexCANAdapter<CAN1> adapterCan1(rawCan1);
VescCANBus vesc(adapterCan1);

// ──────────────────────────────────────────────────────────────
//  Configuration
// ──────────────────────────────────────────────────────────────
#define BAUD_RATE   250000   // VESC utilise 250 kbps par défaut
#define PRINT_MS    1000      // Intervalle d'affichage Serial (ms)

#define COMM_RATE   50      // Intervalle d'envoi de commandes (ms)
#define VESC_ID_A   10       // ID du premier ESC  (régler dans VESC Tool)
#define VESC_ID_B   11       // ID du deuxième ESC (si présent)

// ──────────────────────────────────────────────────────────────
//  Config UART Teensy-Pi
// ──────────────────────────────────────────────────────────────
#define PI_SERIAL Serial1
#define PI_BAUD   115200
                
int i=100;
String inputSerial = "";
String inputPi  = "";
bool displayOn  = true;

enum ControlMode {
  NO_MODE,
  STOP_MODE,
  RPM_MODE,
  CURRENT_MODE,
  DUTY_MODE,
  BRAKE_MODE
};
  
ControlMode currentMode = STOP_MODE;
float targetValue = 0;
unsigned long lastSendA = 0;
unsigned long lastSendB = 0;
void printVescValuesSerial(int id);
void sendVescToScreen(int id);
void sendSensorsToScreen();
void processCommand(String cmd);
void maintainControl(int id, unsigned long &lastSendRef);

// ──────────────────────────────────────────────────────────────
//  SETUP
// ──────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);       // debug USB
  PI_SERIAL.begin(PI_BAUD);   // UART vers Raspberry Pi

  while (!Serial && millis() < 3000);

  rawCan1.begin();            // Initialisation bus CAN 1
  rawCan1.setBaudRate(BAUD_RATE);
  vesc.begin();               // initialise les structures internes

  Serial.println("=== VESC CAN Teensy 4.1 — Prêt ===");
  Serial.printf("CAN1 @ %u bps | ESCs surveillés : %u, %u\n\n",
                BAUD_RATE, VESC_ID_A, VESC_ID_B);

}

// ──────────────────────────────────────────────────────────────
//  LOOP
// ──────────────────────────────────────────────────────────────
uint32_t lastPrintMs = 0;
uint32_t lastCommMs = 0;

void loop() {
  // ── 1. Mise à jour lecture CAN ────────────────────────────────
  vesc.update();   // draine tous les messages en attente

  // ── Commandes depuis Raspberry Pi ─────────────────────
  while (PI_SERIAL.available()) {
    char c = PI_SERIAL.read();
    if (c == '\n') {
      processCommand(inputPi);
      inputPi = "";
    } else {
      inputPi += c;
    }
  }

  // ── Commandes depuis Serial Monitor ───────────────────
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      processCommand(inputSerial);
      inputSerial = "";
    } else {
      inputSerial += c;
    }
  }

  maintainControl(VESC_ID_A, lastSendA);
  maintainControl(VESC_ID_B, lastSendB);

  if (displayOn && millis() - lastPrintMs >= PRINT_MS) {
    lastPrintMs = millis();
    printVescValuesSerial(VESC_ID_A);
    printVescValuesSerial(VESC_ID_B);
  }

  if (millis() - lastCommMs >= COMM_RATE) {
    lastCommMs = millis();
    sendVescToScreen(VESC_ID_A);
    sendVescToScreen(VESC_ID_B);
    sendSensorsToScreen();
  }
}

// Print dans le serial les valeurs des ESCs
void printVescValuesSerial(int id) {
  Serial.print("\n==================================================================================================================================\n");
  Serial.printf("[ESC %u] eRPM=%-8ld  I=%.2f A  Duty=%.1f%%  "
    "Vin=%.1f V  FET=%.1f°C  Mot=%.1f°C\n", id,
    vesc.getERPM(id),
    vesc.getMotorCurrent(id),
    vesc.getDutyCycle(id) * 100.0f,
    vesc.getVoltageIn(id),
    vesc.getTempFET(id),
    vesc.getTempMotor(id));

// ── Calculs ────────────────────────────────────────────
  float inpVoltage    = vesc.getVoltageIn(id);
  float inpCurrent    = vesc.getCurrentIn(id);
  float outCurrent    = vesc.getMotorCurrent(id);
  float duty          = vesc.getDutyCycle(id);
  float rpm           = vesc.getRPM(id, 6);
  float erpm          = vesc.getERPM(id);
  float tachometerAbs = vesc.getTachometer(id);
  float tempMotor     = vesc.getTempMotor(id);
  float tempMosfet    = vesc.getTempFET(id);
  float inpampHours   = vesc.getAmpHours(id);
  float outampHours   = vesc.getAmpHoursChg(id);
  float inpwattHours  = vesc.getWattHours(id);
  float outwattHours  = vesc.getWattHoursChg(id); 

    // Tension moteur estimée (Vbatt × duty)
  float outVoltage  = inpVoltage * duty;

  // Puissances
  float inpPower    = inpVoltage * inpCurrent;
  float outPower    = outVoltage * outCurrent;

  // Efficacité (évite division par zéro)
  float efficiency_wh  = 0;
  float efficiency =0;
  if (inpampHours > 0.2) {
    efficiency_wh = (outwattHours / inpwattHours) * 100.0;
    efficiency_wh = constrain(efficiency_wh, 0, 100);
    efficiency = (outPower / inpPower) * 100.0;
  }

// ── Envoi vers le dashboard ────────────────────────────
  Serial.print(__TIME__);
  Serial.print("\n");
  Serial.printf(">RPM:%.2f\t\t>ERPM:%.2f\t\t>DutyCycle:%.2f\t\t>TachometerAbs:%.2f\t\t>TempMotor:%.2f\t\t>TempMosfet:%.2f\n", 
    rpm, erpm, duty, tachometerAbs, tempMotor, tempMosfet);
  
  Serial.printf(">InpVoltage:%.2f\t\t>InpCurrent:%.2f\t\t>InpPower:%.2f\t\t>InpAmpHours:%.3f\t\t>InpWattHours:%.3f\n", 
    inpVoltage, inpCurrent, inpPower, inpampHours, inpwattHours);

  Serial.printf(">OutVoltage:%.2f\t\t>OutCurrent:%.2f\t\t>OutPower:%.2f\t\t>OutAmpHours:%.3f\t\t>OutWattHours:%.3f\n", 
    outVoltage, outCurrent, outPower, outampHours, outwattHours);

  Serial.printf(">Efficiency(Wh):%.2f\t\t>Efficiency(W):%.2f\n", efficiency_wh*100, efficiency*100);

}

void sendVescToScreen(int id) {
// ── Calculs ────────────────────────────────────────────
  float inpVoltage    = vesc.getVoltageIn(id);
  float inpCurrent    = vesc.getCurrentIn(id);
  float outCurrent    = vesc.getMotorCurrent(id);
  float duty          = vesc.getDutyCycle(id);
  float rpm           = vesc.getRPM(id, 6);
  float erpm          = vesc.getERPM(id);
  float tachometerAbs = vesc.getTachometer(id);
  float tempMotor     = vesc.getTempMotor(id);
  float tempMosfet    = vesc.getTempFET(id);
  float inpampHours   = vesc.getAmpHours(id);
  float outampHours   = vesc.getAmpHoursChg(id);
  float inpwattHours  = vesc.getWattHours(id);
  float outwattHours  = vesc.getWattHoursChg(id); 

    // Tension moteur estimée (Vbatt × duty)
  float outVoltage  = inpVoltage * duty;

  // Puissances
  float inpPower    = inpVoltage * inpCurrent;
  float outPower    = outVoltage * outCurrent;

  // Efficacité (évite division par zéro)
  float efficiency_wh  = 0;
  float efficiency =0;
  if (inpampHours > 0.2) {
    efficiency_wh = (outwattHours / inpwattHours) * 100.0;
    efficiency_wh = constrain(efficiency_wh, 0, 100);
    efficiency = (outPower / inpPower) * 100.0;
  }

  PI_SERIAL.print("{");

  PI_SERIAL.print("\"id\":"); PI_SERIAL.print(id); PI_SERIAL.print(",");
  PI_SERIAL.print("\"vin\":"); PI_SERIAL.print(inpVoltage); PI_SERIAL.print(",");
  PI_SERIAL.print("\"iin\":"); PI_SERIAL.print(inpCurrent); PI_SERIAL.print(",");
  PI_SERIAL.print("\"pin\":"); PI_SERIAL.print(inpPower); PI_SERIAL.print(",");
  PI_SERIAL.print("\"ahin\":"); PI_SERIAL.print(inpampHours); PI_SERIAL.print(",");
  PI_SERIAL.print("\"whin\":"); PI_SERIAL.print(inpwattHours); PI_SERIAL.print(",");

  PI_SERIAL.print("\"vout\":"); PI_SERIAL.print(outVoltage); PI_SERIAL.print(",");
  PI_SERIAL.print("\"iout\":"); PI_SERIAL.print(outCurrent); PI_SERIAL.print(",");
  PI_SERIAL.print("\"pout\":"); PI_SERIAL.print(outPower); PI_SERIAL.print(",");
  PI_SERIAL.print("\"ahout\":"); PI_SERIAL.print(outampHours); PI_SERIAL.print(",");
  PI_SERIAL.print("\"whout\":"); PI_SERIAL.print(outwattHours); PI_SERIAL.print(",");

  PI_SERIAL.print("\"effwh\":"); PI_SERIAL.print(efficiency_wh); PI_SERIAL.print(",");
  PI_SERIAL.print("\"eff\":"); PI_SERIAL.print(efficiency); PI_SERIAL.print(",");
  PI_SERIAL.print("\"rpm\":"); PI_SERIAL.print(rpm); PI_SERIAL.print(",");
  PI_SERIAL.print("\"erpm\":"); PI_SERIAL.print(erpm); PI_SERIAL.print(",");
  PI_SERIAL.print("\"duty\":"); PI_SERIAL.print(duty); PI_SERIAL.print(",");
  PI_SERIAL.print("\"tach\":"); PI_SERIAL.print(tachometerAbs); PI_SERIAL.print(",");

  PI_SERIAL.print("\"tempmot\":"); PI_SERIAL.print(tempMotor); PI_SERIAL.print(",");
  PI_SERIAL.print("\"tempmos\":"); PI_SERIAL.print(tempMosfet);

  PI_SERIAL.println("}");
}

void processCommand(String cmd) {
  cmd.trim(); // Retire les espaces/\r résiduels

  if (cmd == "D" || cmd == "d") {
      displayOn = false;
      Serial.println("Affichage OFF");
      return;
  }
  if (cmd == "S" || cmd == "s") {
      displayOn = true;
      Serial.println("Affichage ON");
      return;
  }

  if (cmd.startsWith("Current=") || cmd.startsWith("current=")) {
    float val = cmd.substring(8).toFloat();
    currentMode = CURRENT_MODE;
    targetValue = val;
    Serial.print("Nouveau courant: ");
    Serial.println(val);
  }

  // BUG CORRIGÉ : substring(13) au lieu de (6) — "BrakeCurrent=" fait 13 caractères
  else if (cmd.startsWith("BrakeCurrent=") || cmd.startsWith("brakecurrent=")) {
    float val = cmd.substring(13).toFloat();
    currentMode = BRAKE_MODE;
    targetValue = val;
    Serial.print("Nouveau courant de frein: ");
    Serial.println(val);
  }

  else if (cmd.startsWith("RPM=") || cmd.startsWith("rpm=")) {
    int val = cmd.substring(4).toInt();
    currentMode = RPM_MODE;
    targetValue = val;
    Serial.print("Nouveau RPM: ");
    Serial.println(val);
  }

  else if (cmd.startsWith("Duty=") || cmd.startsWith("duty=")) {
    float val = cmd.substring(5).toFloat();
    currentMode = DUTY_MODE;
    targetValue = val;
    Serial.print("Nouveau Duty Cycle: ");
    Serial.println(val);
  }

  else if (cmd.startsWith("Stop") or cmd.startsWith("stop") or cmd.startsWith("STOP")) {
    currentMode = STOP_MODE;
    Serial.print("STOPPING...");
  }
}

void maintainControl(int id, unsigned long &lastSendRef) {
  if (currentMode == NO_MODE) return;

  if (millis() - lastSendRef > 20) {
    switch (currentMode) {
      case RPM_MODE:
        vesc.setERPM(id, int(targetValue));
        break;
      case CURRENT_MODE:
        vesc.setCurrent(id, targetValue);
        break;
      case DUTY_MODE:
        vesc.setDuty(id, targetValue);
        break;
      case BRAKE_MODE:
        vesc.setCurrentBrake(id, targetValue);
        break;
      case STOP_MODE:
        vesc.setStop(id);
        break;
      case NO_MODE:
        break; 
      default:
        break;
    }
    lastSendRef = millis();
  }
}

// À appeler dans loop() toutes les ~100 ms
void sendSensorsToScreen() {
  
  float gpsSpeed = 0; // Remplacez par la vraie vitesse GPS
  float imuRoll = 0;  // Remplacez par la vraie valeur IMU
  float imuPitch = 0; // Remplacez par la vraie valeur IMU
  float imuYaw = 0;   // Remplacez par la vraie valeur IMU 
  float sonar1_cm = 0; // Remplacez par la vraie valeur du sonar avant
  float sonar2_cm = 0; // Remplacez par la vraie valeur du sonar
  float sonar3_cm = 0; // Remplacez par la vraie valeur du sonar
  float servo1_deg = 0; // Remplacez par la vraie position du servo
  float servo2_deg = 0; // Remplacez par la vraie position du servo
  float servo3_deg = 0; // Remplacez par la vraie position du servo


  PI_SERIAL.print("{\"type\":\"sensors\"");
  PI_SERIAL.print(",\"speed_kmh\":"); PI_SERIAL.print(gpsSpeed, 1);
  PI_SERIAL.print(",\"roll\":");      PI_SERIAL.print(imuRoll, 2);
  PI_SERIAL.print(",\"pitch\":");     PI_SERIAL.print(imuPitch, 2);
  PI_SERIAL.print(",\"yaw\":");       PI_SERIAL.print(imuYaw, 2);
  PI_SERIAL.print(",\"sonar_avant\":"); PI_SERIAL.print(sonar1_cm, 2);
  PI_SERIAL.print(",\"sonar_arga\":"); PI_SERIAL.print(sonar2_cm, 2);
  PI_SERIAL.print(",\"sonar_ardr\":"); PI_SERIAL.print(sonar3_cm, 2);
  PI_SERIAL.print(",\"servo_avant\":"); PI_SERIAL.print(servo1_deg, 2);
  PI_SERIAL.print(",\"servo_arga\":"); PI_SERIAL.print(servo2_deg, 2);
  PI_SERIAL.print(",\"servo_ardr\":"); PI_SERIAL.print(servo3_deg, 2);
  PI_SERIAL.print(",\"state\":\"RUN\"");
  PI_SERIAL.println("}");
}





