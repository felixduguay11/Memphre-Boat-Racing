/*
 * VescCAN_Teensy.cpp
 * Implémentation de la bibliothèque VESC CAN pour Teensy 4.1
 */

#include "VescCAN_Teensy.h"

// ─────────────────────────────────────────────────────────────
//  Constructeur
// ─────────────────────────────────────────────────────────────
VescCANBus::VescCANBus(IFlexCANAdapter& adapter) : _adapter(adapter) {
  memset(_data, 0, sizeof(_data));
}

// ─────────────────────────────────────────────────────────────
//  begin() — initialisation du bus CAN
// ─────────────────────────────────────────────────────────────
void VescCANBus::begin(uint32_t baudrate) {
  // NOTE : begin() et setBaudRate() sont appelés sur l'objet
  // FlexCAN_T4 directement depuis le sketch utilisateur (voir
  // exemple multi_vesc_reader.ino), car les templates FlexCAN_T4
  // ne partagent pas de classe de base commune.
  // Cette méthode sert à initialiser les structures internes.
  memset(_data, 0, sizeof(_data));
  (void)baudrate; // baudrate géré côté sketch via begin()/setBaudRate()
}

// ─────────────────────────────────────────────────────────────
//  update() — à appeler dans loop()
// ─────────────────────────────────────────────────────────────
void VescCANBus::update() {
  CAN_message_t msg;
  // Draine tous les messages en attente (évite la saturation du buffer)
  while (_adapter.read(msg)) {
    _decodeFrame(msg);
  }
}

// ─────────────────────────────────────────────────────────────
//  Décodage d'une trame CAN VESC
// ─────────────────────────────────────────────────────────────
void VescCANBus::_decodeFrame(const CAN_message_t& msg) {
  // Les VESC utilisent des trames étendues (29-bit ID)
  if (!msg.flags.extended) return;

  uint8_t vesc_id = (uint8_t)(msg.id & 0xFF);          // bits  0-7  → ID du VESC
  uint8_t cmd_id  = (uint8_t)((msg.id >> 8) & 0xFF);   // bits 8-15  → commande

  VescData& v = _data[vesc_id];
  v.updated         = true;
  v.last_update_ms  = millis();

  switch (cmd_id) {

    // ── Status 1 : eRPM, courant moteur, duty cycle ────────────
    // Layout : Int32 eRPM | Int16 current×10 | Int16 duty×1000
    case CAN_PACKET_STATUS:
      v.erpm          = _readInt32BE(msg.buf, 0);
      v.current_motor = _readInt16BE(msg.buf, 4) / 10.0f;
      v.duty_cycle    = _readInt16BE(msg.buf, 6) / 1000.0f;
      break;

    // ── Status 2 : Ah consommées / rechargées ──────────────────
    // Layout : Int32 Ah×10000 | Int32 Ah_chg×10000
    case CAN_PACKET_STATUS_2:
      v.amp_hours     = _readInt32BE(msg.buf, 0) / 10000.0f;
      v.amp_hours_chg = _readInt32BE(msg.buf, 4) / 10000.0f;
      break;

    // ── Status 3 : Wh consommées / rechargées ──────────────────
    // Layout : Int32 Wh×10000 | Int32 Wh_chg×10000
    // NOTE : la lib originale utilisait ×0.01 (bug), le firmware
    //        VESC confirme ÷10000 (voir vesc_c/comm_can.c)
    case CAN_PACKET_STATUS_3:
      v.watt_hours     = _readInt32BE(msg.buf, 0) / 10000.0f;
      v.watt_hours_chg = _readInt32BE(msg.buf, 4) / 10000.0f;
      break;

    // ── Status 4 : Températures, courant entrée, PID ───────────
    // Layout : Int16 FET×10 | Int16 Motor×10 | Int16 Iin×10 | Int16 PID×50
    case CAN_PACKET_STATUS_4:
      v.temp_fet   = _readInt16BE(msg.buf, 0) / 10.0f;
      v.temp_motor = _readInt16BE(msg.buf, 2) / 10.0f;
      v.current_in = _readInt16BE(msg.buf, 4) / 10.0f;
      v.pid_pos    = _readInt16BE(msg.buf, 6) / 50.0f;
      break;

    // ── Status 5 : Tachomètre, tension batterie ─────────────────
    // Layout : Int32 tachometer | Int16 voltage×10 | Int16 reserved
    case CAN_PACKET_STATUS_5:
      v.tachometer = _readInt32BE(msg.buf, 0);
      v.voltage_in = _readInt16BE(msg.buf, 4) / 10.0f;
      break;

    default:
      // Trame non reconnue — ignorée silencieusement
      // (pour du debug, décommentez les lignes ci-dessous)
      // Serial.printf("[RAW] VESC=%u CMD=%u ID=0x%08X\n",
      //               vesc_id, cmd_id, msg.id);
      break;
  }
}

// ─────────────────────────────────────────────────────────────
//  Envoi d'une commande SET (Int32 big-endian, 4 octets)
// ─────────────────────────────────────────────────────────────
bool VescCANBus::_sendCmd(uint8_t vesc_id, uint8_t cmd, int32_t value) {
  CAN_message_t msg;
  msg.id    = ((uint32_t)cmd << 8) | vesc_id;
  msg.flags.extended = 1;  // ID étendu 29-bit obligatoire
  msg.len   = 4;
  _writeInt32BE(msg.buf, value);
  return _adapter.write(msg);
}

// ─────────────────────────────────────────────────────────────
//  Commandes SET publiques
// ─────────────────────────────────────────────────────────────

bool VescCANBus::setDuty(uint8_t id, float duty) {
  // Clamp entre -1.0 et 1.0
  if (duty >  1.0f) duty =  1.0f;
  if (duty < -1.0f) duty = -1.0f;
  int32_t value = (int32_t)(duty * 100000.0f);
  return _sendCmd(id, CAN_PACKET_SET_DUTY, value);
}

bool VescCANBus::setCurrent(uint8_t id, float current_A) {
  int32_t value = (int32_t)(current_A * 1000.0f);  // mA
  return _sendCmd(id, CAN_PACKET_SET_CURRENT, value);
}

bool VescCANBus::setCurrentBrake(uint8_t id, float current_A) {
  if (current_A < 0) current_A = -current_A;  // toujours positif
  int32_t value = (int32_t)(current_A * 1000.0f);
  return _sendCmd(id, CAN_PACKET_SET_CURRENT_BRAKE, value);
}

bool VescCANBus::setERPM(uint8_t id, int32_t erpm) {
  return _sendCmd(id, CAN_PACKET_SET_RPM, erpm);
}

bool VescCANBus::setPosition(uint8_t id, float degrees) {
  int32_t value = (int32_t)(degrees * 1000000.0f);
  return _sendCmd(id, CAN_PACKET_SET_POS, value);
}

bool VescCANBus::setStop(uint8_t id) {
  return setDuty(id, 0.0f);
}

// ─────────────────────────────────────────────────────────────
//  Getters
// ─────────────────────────────────────────────────────────────

bool VescCANBus::isUpdated(uint8_t id) const {
  return _data[id].updated;
}

uint32_t VescCANBus::lastUpdateMs(uint8_t id) const {
  return _data[id].last_update_ms;
}

const VescData& VescCANBus::getData(uint8_t id) const {
  return _data[id];
}

int32_t VescCANBus::getERPM(uint8_t id) const {
  return _data[id].erpm;
}

float VescCANBus::getMotorCurrent(uint8_t id) const {
  return _data[id].current_motor;
}

float VescCANBus::getDutyCycle(uint8_t id) const {
  return _data[id].duty_cycle;
}

float VescCANBus::getAmpHours(uint8_t id) const {
  return _data[id].amp_hours;
}

float VescCANBus::getAmpHoursChg(uint8_t id) const {
  return _data[id].amp_hours_chg;
}

float VescCANBus::getWattHours(uint8_t id) const {
  return _data[id].watt_hours;
}

float VescCANBus::getWattHoursChg(uint8_t id) const {
  return _data[id].watt_hours_chg;
}

float VescCANBus::getTempFET(uint8_t id) const {
  return _data[id].temp_fet;
}

float VescCANBus::getTempMotor(uint8_t id) const {
  return _data[id].temp_motor;
}

float VescCANBus::getCurrentIn(uint8_t id) const {
  return _data[id].current_in;
}

float VescCANBus::getPIDPos(uint8_t id) const {
  return _data[id].pid_pos;
}

int32_t VescCANBus::getTachometer(uint8_t id) const {
  return _data[id].tachometer;
}

float VescCANBus::getVoltageIn(uint8_t id) const {
  return _data[id].voltage_in;
}

float VescCANBus::getRPM(uint8_t id, uint8_t pole_pairs) const {
  if (pole_pairs == 0) return 0.0f;
  return (float)_data[id].erpm / (float)pole_pairs;
}

void VescCANBus::resetData(uint8_t id) {
  memset(&_data[id], 0, sizeof(VescData));
}

// ─────────────────────────────────────────────────────────────
//  Affichage debug
// ─────────────────────────────────────────────────────────────
void VescCANBus::printData(uint8_t id) const {
  const VescData& v = _data[id];
  if (!v.updated) {
    Serial.printf("[VESC %u] Aucune donnée reçue.\n", id);
    return;
  }
  Serial.printf("── VESC ID %u ─────────────────────────\n", id);
  Serial.printf("  eRPM       : %ld\n",      v.erpm);
  Serial.printf("  I moteur   : %.2f A\n",   v.current_motor);
  Serial.printf("  Duty       : %.1f %%\n",  v.duty_cycle * 100.0f);
  Serial.printf("  Ah         : %.4f Ah\n",  v.amp_hours);
  Serial.printf("  Ah (chg)   : %.4f Ah\n",  v.amp_hours_chg);
  Serial.printf("  Wh         : %.4f Wh\n",  v.watt_hours);
  Serial.printf("  Wh (chg)   : %.4f Wh\n",  v.watt_hours_chg);
  Serial.printf("  Temp FET   : %.1f °C\n",  v.temp_fet);
  Serial.printf("  Temp Mot   : %.1f °C\n",  v.temp_motor);
  Serial.printf("  I entrée   : %.2f A\n",   v.current_in);
  Serial.printf("  PID pos    : %.2f °\n",   v.pid_pos);
  Serial.printf("  Tacho      : %ld\n",      v.tachometer);
  Serial.printf("  V entrée   : %.2f V\n",   v.voltage_in);
  Serial.printf("  Dernier msg: %lu ms\n",   v.last_update_ms);
  Serial.println();
}

void VescCANBus::printAllUpdated() const {
  for (uint16_t i = 0; i < MAX_VESC_ID; i++) {
    if (_data[i].updated) {
      printData((uint8_t)i);
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  Helpers big-endian statiques
// ─────────────────────────────────────────────────────────────
int32_t VescCANBus::_readInt32BE(const uint8_t* buf, int offset) {
  return ((int32_t)buf[offset]     << 24) |
         ((int32_t)buf[offset + 1] << 16) |
         ((int32_t)buf[offset + 2] <<  8) |
         ((int32_t)buf[offset + 3]);
}

int16_t VescCANBus::_readInt16BE(const uint8_t* buf, int offset) {
  return (int16_t)(((uint16_t)buf[offset] << 8) | buf[offset + 1]);
}

void VescCANBus::_writeInt32BE(uint8_t* buf, int32_t value) {
  buf[0] = (uint8_t)((value >> 24) & 0xFF);
  buf[1] = (uint8_t)((value >> 16) & 0xFF);
  buf[2] = (uint8_t)((value >>  8) & 0xFF);
  buf[3] = (uint8_t)( value        & 0xFF);
}
