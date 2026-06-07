#pragma once

#include <arduino_freertos.h>
#include <semphr.h>
#include "config.h"

// ─── XBUS Constants ───────────────────────────────────────────────────────────
#define XBUS_PREAMBLE       0xFA
#define XBUS_BID            0xFF
#define MID_WAKEUP          0x3E
#define MID_GOTOMEASURE     0x10
#define MID_GOTOMEASURE_ACK 0x11
#define MID_MTDATA2         0x36
#define MID_ERROR           0x42

// ─── XDA Identifiers ──────────────────────────────────────────────────────────
#define XDA_EULER_ANGLES    0x2030
#define XDA_LAT_LON         0x5020
#define XDA_ALT             0x5030
#define XDA_VELOCITY_XYZ    0xD010

// Limite max de bytes lus par appel à update()
// Empêche update() de boucler trop longtemps si le buffer
// UART est plein et bloque les autres tâches FreeRTOS
#define XSENS_MAX_BYTES_PER_UPDATE  256

// =====================================================
// Struct partagée — données Xsens MTi-670
// =====================================================
struct XsensData {
    float roll, pitch, yaw;
    bool  att_valid;
    double lat, lon;
    float  altitude;
    bool   pos_valid, alt_valid;
    float vx, vy, vz, speed;
    bool  vel_valid;
    float temps_us;
};

extern XsensData Xsens_data;
extern float     Xsens_temps_us;

// ─── Driver MTi670 ────────────────────────────────────────────────────────────
class MTi670 {
public:
    MTi670(HardwareSerial& serial, uint32_t baud = BAUD_Xsens);

    // Initialise le port série — ne bloque PAS sur l'attente WakeUp.
    // La logique de WakeUp/GoToMeasurement est gérée par la tâche
    // FreeRTOS qui peut utiliser vTaskDelay() proprement.
    void begin();

    // Envoie une commande GoToMeasurement (non bloquant)
    void requestMeasurement();

    // Parse les bytes disponibles — limite stricte pour ne pas
    // bloquer les autres tâches si le buffer UART est plein
    bool update();

    // État de l'initialisation
    bool isReady() const { return _gotWakeUp || _newData; }

private:
    HardwareSerial& _serial;
    uint32_t        _baud;

    float  _roll  = 0.0f, _pitch    = 0.0f, _yaw      = 0.0f;
    bool   _att_valid = false;
    double _lat   = 0.0,  _lon      = 0.0;
    float  _altitude  = 0.0f;
    bool   _pos_valid = false, _alt_valid = false;
    float  _vx    = 0.0f, _vy = 0.0f, _vz = 0.0f, _speed = 0.0f;
    bool   _vel_valid = false;

    bool _gotWakeUp = false;
    bool _newData   = false;

    enum class State { WAIT_PRE, WAIT_BID, WAIT_MID, WAIT_LEN, WAIT_DATA, WAIT_CHK };
    State   _state  = State::WAIT_PRE;
    uint8_t _pkt[512];
    int     _pktIdx = 0;
    uint8_t _mid    = 0;
    uint8_t _len    = 0;

    void    feedByte(uint8_t b);
    void    processPacket();
    void    parseMTData2(uint8_t* payload, uint8_t plen);
    void    sendMsg(uint8_t mid);
    uint8_t checksum(const uint8_t* data, int len) const;

    static float  beFloat(const uint8_t* p);
    static double beDouble(const uint8_t* p);

    friend void Task_Xsens(void *ptr);
};

// =====================================================
// Tâche FreeRTOS
// =====================================================
void Task_Xsens(void *ptr);