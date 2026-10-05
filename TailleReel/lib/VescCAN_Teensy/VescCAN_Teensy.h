/*
 * VescCAN_Teensy.h
 * Bibliothèque VESC CAN pour Teensy 4.1 + FlexCAN_T4
 *
 * Fonctionnalités :
 *  - Lecture temps réel de tous les status VESC (Status 1..5)
 *  - Envoi de commandes moteur (duty, courant, RPM, position, frein)
 *  - Support multi-ESC (jusqu'à 256 IDs sur le même bus)
 *  - Compatible FlexCAN_T4 (CAN1, CAN2, CAN3)
 *
 * Usage typique :
 *
 *   #include "VescCAN_Teensy.h"
 *   FlexCAN_T4<CAN1, RX_SIZE_256, TX_SIZE_16> can1;
 *   VescCANBus vescBus(can1);
 *
 *   void setup() { vescBus.begin(250000); }
 *   void loop()  { vescBus.update();
 *                  float rpm = vescBus.getERPM(10); // ESC ID=10
 *                  vescBus.setDuty(10, 0.2f);       // 20% sur ESC ID=10 }
 */

#pragma once
#ifndef VESC_CAN_TEENSY_H
#define VESC_CAN_TEENSY_H

#include <Arduino.h>
#include <FlexCAN_T4.h>

// ─────────────────────────────────────────────────────────────
//  Codes de commande CAN VESC  (bits 8-15 de l'ID étendu)
// ─────────────────────────────────────────────────────────────
// Trames STATUS (ESC → Teensy)
#define CAN_PACKET_STATUS    9    // eRPM, courant moteur, duty cycle
#define CAN_PACKET_STATUS_2 14    // Amp-heures (consommées + chargées)
#define CAN_PACKET_STATUS_3 15    // Watt-heures (consommées + chargées)
#define CAN_PACKET_STATUS_4 16    // Temp FET, Temp moteur, courant entrée, PID pos
#define CAN_PACKET_STATUS_5 27    // Tachomètre, tension d'entrée

// Trames SET (Teensy → ESC)
#define CAN_PACKET_SET_DUTY          0    // Duty cycle  (-1.0 à 1.0) × 100000
#define CAN_PACKET_SET_CURRENT       1    // Courant moteur (mA)
#define CAN_PACKET_SET_CURRENT_BRAKE 2    // Courant de freinage (mA)
#define CAN_PACKET_SET_RPM           3    // RPM électrique × 30 (= eRPM)
#define CAN_PACKET_SET_POS           4    // Position (degrés) × 1000000

// ─────────────────────────────────────────────────────────────
//  Structure de données par ESC
// ─────────────────────────────────────────────────────────────
struct VescData {
  // Status 1 — CAN_PACKET_STATUS
  int32_t  erpm;            // RPM électrique (÷ nb_paires_pôles = RPM méca)
  float    current_motor;   // Courant moteur (A)
  float    duty_cycle;      // Duty cycle (-1.0 à 1.0)

  // Status 2 — CAN_PACKET_STATUS_2
  float    amp_hours;       // Ah consommées
  float    amp_hours_chg;   // Ah rechargées (regen)

  // Status 3 — CAN_PACKET_STATUS_3
  float    watt_hours;      // Wh consommées
  float    watt_hours_chg;  // Wh rechargées (regen)

  // Status 4 — CAN_PACKET_STATUS_4
  float    temp_fet;        // Température des FETs (°C)
  float    temp_motor;      // Température moteur (°C)
  float    current_in;      // Courant d'entrée / batterie (A)
  float    pid_pos;         // Position PID (degrés)

  // Status 5 — CAN_PACKET_STATUS_5
  int32_t  tachometer;      // Tachomètre (ERPM cumulé)
  float    voltage_in;      // Tension batterie (V)

  // Méta
  bool     updated;         // true = au moins une trame reçue
  uint32_t last_update_ms;  // millis() du dernier paquet reçu
};

// ─────────────────────────────────────────────────────────────
//  Classe principale VescCANBus
//
//  Le paramètre template permet de passer CAN1, CAN2 ou CAN3.
//  Pour les rendre interchangeables, on utilise une abstraction
//  minimale via des pointeurs de fonction stockés dans begin().
// ─────────────────────────────────────────────────────────────

// Interface abstraite légère pour éviter l'héritage complet
// sur les templates FlexCAN (qui ne partagent pas de classe base)
class IFlexCANAdapter {
public:
  virtual bool read(CAN_message_t& msg) = 0;
  virtual bool write(const CAN_message_t& msg) = 0;
};

template <CAN_DEV_TABLE DEV>
class FlexCANAdapter : public IFlexCANAdapter {
public:
  FlexCAN_T4<DEV, RX_SIZE_256, TX_SIZE_16>& bus;
  explicit FlexCANAdapter(FlexCAN_T4<DEV, RX_SIZE_256, TX_SIZE_16>& b) : bus(b) {}
  bool read(CAN_message_t& msg)         override { return bus.read(msg); }
  bool write(const CAN_message_t& msg)  override { return bus.write(msg); }
};

// ─────────────────────────────────────────────────────────────
//  VescCANBus — classe principale
// ─────────────────────────────────────────────────────────────
class VescCANBus {
public:
  static constexpr uint16_t MAX_VESC_ID = 256;

  // Constructeur — accepte n'importe quel adaptateur FlexCAN
  explicit VescCANBus(IFlexCANAdapter& adapter);

  // ── Initialisation ──────────────────────────────────────────
  void begin(uint32_t baudrate = 250000);

  // ── Mise à jour (appeler dans loop()) ───────────────────────
  // Draine tous les messages disponibles et met à jour vescData[]
  void update();

  // ── Getters — données moteur ─────────────────────────────────
  // id = numéro VESC (0-255), ex. : id=10 si VESC App ID = 10
  bool    isUpdated(uint8_t id) const;
  uint32_t lastUpdateMs(uint8_t id) const;
  const VescData& getData(uint8_t id) const;

  int32_t  getERPM(uint8_t id)          const;
  float    getMotorCurrent(uint8_t id)  const;
  float    getDutyCycle(uint8_t id)     const;
  float    getAmpHours(uint8_t id)      const;
  float    getAmpHoursChg(uint8_t id)   const;
  float    getWattHours(uint8_t id)     const;
  float    getWattHoursChg(uint8_t id)  const;
  float    getTempFET(uint8_t id)       const;
  float    getTempMotor(uint8_t id)     const;
  float    getCurrentIn(uint8_t id)     const;
  float    getPIDPos(uint8_t id)        const;
  int32_t  getTachometer(uint8_t id)    const;
  float    getVoltageIn(uint8_t id)     const;

  // RPM mécanique si vous connaissez le nombre de paires de pôles
  float    getRPM(uint8_t id, uint8_t pole_pairs) const;

  // ── Commandes SET — envoi vers ESC ───────────────────────────
  // Duty cycle : -1.0 (plein arrière) à +1.0 (plein avant)
  bool setDuty(uint8_t id, float duty);

  // Courant moteur en Ampères (positif = moteur, négatif = regen)
  bool setCurrent(uint8_t id, float current_A);

  // Courant de freinage en Ampères (toujours positif)
  bool setCurrentBrake(uint8_t id, float current_A);

  // RPM électrique cible (eRPM = RPM_meca × nb_paires_pôles)
  bool setERPM(uint8_t id, int32_t erpm);

  // Position en degrés (mode PID position, 0.0 à 360.0)
  bool setPosition(uint8_t id, float degrees);

  // Arrêt immédiat (duty = 0)
  bool setStop(uint8_t id);

  // ── Utilitaires ───────────────────────────────────────────────
  // Affiche sur Serial toutes les données d'un ESC
  void printData(uint8_t id) const;

  // Affiche tous les ESCs ayant reçu au moins une trame
  void printAllUpdated() const;

  // Réinitialise les données d'un ESC
  void resetData(uint8_t id);

private:
  IFlexCANAdapter& _adapter;
  VescData         _data[MAX_VESC_ID];

  // Décodage d'une trame CAN reçue
  void _decodeFrame(const CAN_message_t& msg);

  // Envoi d'une trame SET (Int32 big-endian sur 4 octets)
  bool _sendCmd(uint8_t vesc_id, uint8_t cmd, int32_t value);

  // Helpers encodage/décodage big-endian
  static int32_t  _readInt32BE(const uint8_t* buf, int offset);
  static int16_t  _readInt16BE(const uint8_t* buf, int offset);
  static void     _writeInt32BE(uint8_t* buf, int32_t value);
};

// ─────────────────────────────────────────────────────────────
//  Macros de convenance pour déclarer rapidement un bus
// ─────────────────────────────────────────────────────────────
// Usage :
//   VESC_CAN_DECLARE(bus1, CAN1);
//   VescCANBus vesc(bus1);
#define VESC_CAN_DECLARE(name, CAN_DEV) \
  FlexCAN_T4<CAN_DEV, RX_SIZE_256, TX_SIZE_16> _raw_##name; \
  FlexCANAdapter<CAN_DEV> name(_raw_##name)

#endif // VESC_CAN_TEENSY_H
