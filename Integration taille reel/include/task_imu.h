#pragma once

#include <arduino_freertos.h>
#include <semphr.h>

// ─── XBUS Constants ───────────────────────────────────────────────────────────
#define XBUS_PREAMBLE    0xFA
#define XBUS_BID         0xFF
#define MID_WAKEUP       0x3E
#define MID_GOTOMEASURE  0x10
#define MID_MTDATA2      0x36
#define MID_ERROR        0x42

// ─── XDA Identifiers ──────────────────────────────────────────────────────────
#define XDA_EULER_ANGLES 0x2030  // Roll, Pitch, Yaw (float32, deg)
#define XDA_LAT_LON      0x5020  // Latitude, Longitude (float64, deg)
#define XDA_VELOCITY_XYZ 0xD010  // Velocity NED (float32, m/s)

// =====================================================
// Paramètres tâche IMU
// =====================================================
const uint32_t IMU_BAUDRATE   = 115200;  // baudrate MTi-670
const int      PERIODE_IMU_MS = 10;    // 100Hz

// =====================================================
// Struct partagée — données IMU complètes
// Protégée par dataMutex
// =====================================================
struct IMUData {
    float  roll;        // degrés
    float  pitch;       // degrés
    float  yaw;         // degrés
    float  vx;          // m/s (NED frame)
    float  vy;          // m/s
    float  vz;          // m/s
    float  speed;       // vitesse horizontale (m/s)
    double lat;         // degrés
    double lon;         // degrés
    bool   att_valid;
    bool   vel_valid;
    bool   pos_valid;
    float  temps_us;
};

// =====================================================
// Variable partagée — écrite par Task_IMU
// Lue par Task_RPi et Task_FoilsControl via dataMutex
// =====================================================
extern IMUData g_imu;

// ─── Driver MTi670 ────────────────────────────────────────────────────────────
class MTi670 {
public:
    MTi670(HardwareSerial& serial, uint32_t baud = IMU_BAUDRATE);

    void begin();
    bool update();  // retourne true si nouveau paquet reçu

private:
    HardwareSerial& _serial;
    uint32_t        _baud;

    // Données internes — copiées dans g_imu à chaque paquet
    float  _roll  = 0.0f, _pitch = 0.0f, _yaw  = 0.0f;
    float  _vx    = 0.0f, _vy    = 0.0f, _vz   = 0.0f;
    float  _speed = 0.0f;
    double _lat   = 0.0,  _lon   = 0.0;
    bool   _att_valid = false;
    bool   _vel_valid = false;
    bool   _pos_valid = false;

    enum class State { WAIT_PRE, WAIT_BID, WAIT_MID, WAIT_LEN, WAIT_DATA, WAIT_CHK };
    State   _state  = State::WAIT_PRE;
    uint8_t _pkt[512];
    int     _pktIdx = 0;
    uint8_t _mid    = 0;
    uint8_t _len    = 0;
    bool    _newData = false;

    void    feedByte(uint8_t b);
    void    processPacket();
    void    parseMTData2(uint8_t* payload, uint8_t plen);
    void    sendMsg(uint8_t mid);
    uint8_t checksum(const uint8_t* data, int len) const;

    static float  beFloat(const uint8_t* p);
    static double beDouble(const uint8_t* p);

    // Accès aux données parsées — utilisé par Task_IMU
    friend void Task_IMU(void *ptr);
};

// =====================================================
// Tâche FreeRTOS
// MTi-670 sur Serial5 — pins 21(RX) / 20(TX) Teensy 4.1
// =====================================================
void Task_IMU(void *ptr);
