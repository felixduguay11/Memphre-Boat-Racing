#pragma once
#include <Arduino.h>

// ─── XBUS Constants ───────────────────────────────────────────────────────────
#define XBUS_PREAMBLE       0xFA
#define XBUS_BID            0xFF
#define MID_WAKEUP          0x3E
#define MID_GOTOMEASURE     0x10
#define MID_GOTOMEASURE_ACK 0x11
#define MID_MTDATA2         0x36
#define MID_ERROR           0x42

// ─── XDA Identifiers ─────────────────────────────────────────────────────────
#define XDA_EULER_ANGLES 0x2030  // Roll, Pitch, Yaw (float32, deg)
#define XDA_LAT_LON      0x5020  // Latitude, Longitude (float64, deg)
#define XDA_ALT          0x5030  // Altitude ellipsoid (float32, m)
#define XDA_VELOCITY_XYZ 0xD010  // Velocity NED (float32, m/s)

// ─── Data Structs ─────────────────────────────────────────────────────────────
struct MTiAttitude {
    float roll, pitch, yaw;  // degrees
    bool  valid = false;
};

struct MTiPosition {
    double lat, lon;          // degrees
    float  altitude;          // metres
    bool   valid    = false;
    bool   altValid = false;
};

struct MTiVelocity {
    float vx, vy, vz;        // m/s (NED frame)
    float speed;             // horizontal speed magnitude (m/s)
    bool  valid = false;
};

// ─── Driver ───────────────────────────────────────────────────────────────────
class MTi670 {
public:
    MTi670(HardwareSerial& serial, uint32_t baud = 115200);

    // Call in setup() — blocks until WakeUp received or timeout
    void begin(uint32_t timeoutMs = 5000);

    // Call in loop() — returns true on new packet
    bool update();

    const MTiAttitude& attitude() const { return _att; }
    const MTiPosition& position() const { return _pos; }
    const MTiVelocity& velocity() const { return _vel; }

    void goToMeasurement();
    void printData() const;

private:
    HardwareSerial& _serial;
    uint32_t        _baud;

    MTiAttitude _att;
    MTiPosition _pos;
    MTiVelocity _vel;

    bool _gotWakeUp = false;
    bool _gotAck    = false;
    bool _newData   = false;

    enum class State { WAIT_PRE, WAIT_BID, WAIT_MID, WAIT_LEN, WAIT_DATA, WAIT_CHK };
    State   _state   = State::WAIT_PRE;
    uint8_t _pkt[512];
    int     _pktIdx  = 0;
    uint8_t _mid     = 0;
    uint8_t _len     = 0;

    void    feedByte(uint8_t b);
    void    processPacket();
    void    parseMTData2(uint8_t* payload, uint8_t plen);
    void    sendMsg(uint8_t mid);
    uint8_t checksum(const uint8_t* data, int len) const;

    static float   beFloat(const uint8_t* p);
    static double  beDouble(const uint8_t* p);
};