#include "MTi670.h"

// ─── Constructor / begin ──────────────────────────────────────────────────────

MTi670::MTi670(HardwareSerial& serial, uint32_t baud)
    : _serial(serial), _baud(baud) {}

void MTi670::begin() {
    _serial.begin(_baud);

    // Wait for serial to be ready
    delay(100);

    // Flush any garbage on the line
    while (_serial.available()) _serial.read();

    // Send GoToMeasurement — retry several times to handle slow boot
    for (int i = 0; i < 5; i++) {
        sendMsg(MID_GOTOMEASURE);
        delay(200);
    }
}

// ─── update() ────────────────────────────────────────────────────────────────

bool MTi670::update() {
    _newData = false;
    while (_serial.available()) {
        feedByte((uint8_t)_serial.read());
    }
    return _newData;
}

// ─── goToMeasurement() ───────────────────────────────────────────────────────

void MTi670::goToMeasurement() {
    sendMsg(MID_GOTOMEASURE);
}

// ─── printData() ─────────────────────────────────────────────────────────────

void MTi670::printData() const {
    if (_att.valid) {
        Serial.printf("Roll: %6.2f  Pitch: %6.2f  Yaw: %6.2f  deg\n",
            _att.roll, _att.pitch, _att.yaw);
    }
    if (_pos.valid) {
        Serial.printf("Lat: %.7f  Lon: %.7f\n", _pos.lat, _pos.lon);
    }
    if (_vel.valid) {
        Serial.printf("Speed: %.2f m/s  (vx:%.2f  vy:%.2f  vz:%.2f)\n",
            _vel.speed, _vel.vx, _vel.vy, _vel.vz);
    }
}

// ─── XBUS send ────────────────────────────────────────────────────────────────

void MTi670::sendMsg(uint8_t mid) {
    uint8_t msg[5];
    msg[0] = XBUS_PREAMBLE;
    msg[1] = XBUS_BID;
    msg[2] = mid;
    msg[3] = 0x00;
    msg[4] = checksum(&msg[2], 2);
    _serial.write(msg, 5);
}

uint8_t MTi670::checksum(const uint8_t* data, int len) const {
    uint8_t sum = 0;
    for (int i = 0; i < len; i++) sum += data[i];
    return (uint8_t)(0x100 - sum);
}

// ─── State machine ────────────────────────────────────────────────────────────

void MTi670::feedByte(uint8_t b) {
    switch (_state) {
        case State::WAIT_PRE:
            if (b == XBUS_PREAMBLE) {
                _pktIdx = 0;
                _pkt[_pktIdx++] = b;
                _state = State::WAIT_BID;
            }
            break;

        case State::WAIT_BID:
            _pkt[_pktIdx++] = b;
            _state = (b == XBUS_BID) ? State::WAIT_MID : State::WAIT_PRE;
            break;

        case State::WAIT_MID:
            _mid = b;
            _pkt[_pktIdx++] = b;
            _state = State::WAIT_LEN;
            break;

        case State::WAIT_LEN:
            _len = b;
            _pkt[_pktIdx++] = b;
            _state = (_len > 0) ? State::WAIT_DATA : State::WAIT_CHK;
            break;

        case State::WAIT_DATA:
            _pkt[_pktIdx++] = b;
            if (_pktIdx == 4 + _len) _state = State::WAIT_CHK;
            break;

        case State::WAIT_CHK: {
            _pkt[_pktIdx++] = b;
            uint8_t sum = 0;
            for (int i = 1; i < _pktIdx; i++) sum += _pkt[i];
            if (sum == 0x00) {
                processPacket();
            } else {
                Serial.printf("[MTi670] Checksum error (sum=0x%02X len=%d MID=0x%02X)\n", sum, _pktIdx, _mid);
            }
            _state = State::WAIT_PRE;
            break;
        }
    }
}

// ─── Packet dispatch ──────────────────────────────────────────────────────────

void MTi670::processPacket() {
    switch (_mid) {
        case MID_WAKEUP:
            Serial.println("[MTi670] WakeUp — sending GoToMeasurement");
            sendMsg(MID_GOTOMEASURE);
            break;

        case MID_MTDATA2:
            parseMTData2(&_pkt[4], _len);
            _newData = true;
            break;

        case MID_ERROR:
            Serial.printf("[MTi670] Error: 0x%02X\n", _pkt[4]);
            break;

        default:
            // Respond to any other message with GoToMeasurement
            // in case device is in config mode
            Serial.printf("[MTi670] MID: 0x%02X — sending GoToMeasurement\n", _mid);
            sendMsg(MID_GOTOMEASURE);
            break;
    }
}

// ─── MTData2 / XDA parser ─────────────────────────────────────────────────────

void MTi670::parseMTData2(uint8_t* p, uint8_t plen) {
    int i = 0;
    while (i + 2 < plen) {
        uint16_t id  = ((uint16_t)p[i] << 8) | p[i + 1];
        uint8_t  len = p[i + 2];
        uint8_t* d   = &p[i + 3];
        i += 3 + len;

        switch (id) {
            case XDA_EULER_ANGLES:
                if (len >= 12) {
                    _att.roll  = beFloat(d);
                    _att.pitch = beFloat(d + 4);
                    _att.yaw   = beFloat(d + 8);
                    _att.valid = true;
                }
                break;

            case XDA_LAT_LON:
                if (len >= 16) {
                    _pos.lat   = beDouble(d);
                    _pos.lon   = beDouble(d + 8);
                    _pos.valid = true;
                }
                break;

            case XDA_VELOCITY_XYZ:
                if (len >= 12) {
                    _vel.vx    = beFloat(d);
                    _vel.vy    = beFloat(d + 4);
                    _vel.vz    = beFloat(d + 8);
                    _vel.speed = sqrtf(_vel.vx * _vel.vx + _vel.vy * _vel.vy);
                    _vel.valid = true;
                }
                break;
        }
    }
}

// ─── Big-endian helpers ───────────────────────────────────────────────────────

float MTi670::beFloat(const uint8_t* p) {
    uint32_t u = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                 ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
    float f; memcpy(&f, &u, 4); return f;
}

double MTi670::beDouble(const uint8_t* p) {
    uint64_t u = ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
                 ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
                 ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
                 ((uint64_t)p[6] <<  8) |  (uint64_t)p[7];
    double d; memcpy(&d, &u, 8); return d;
}