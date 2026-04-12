#include "task_imu.h"

// =====================================================
// Définition de la variable partagée
// déclarée extern dans task_imu.h
// =====================================================
IMUData g_imu = {
    .roll      = 0.0f,
    .pitch     = 0.0f,
    .yaw       = 0.0f,
    .vx        = 0.0f,
    .vy        = 0.0f,
    .vz        = 0.0f,
    .speed     = 0.0f,
    .lat       = 0.0,
    .lon       = 0.0,
    .att_valid = false,
    .vel_valid = false,
    .pos_valid = false,
    .temps_us  = 0.0f
};

// Mutex défini dans main.cpp
extern SemaphoreHandle_t dataMutex;

// Instance du driver — Serial5 = pins 21(RX) / 20(TX) sur Teensy 4.1
static MTi670 s_mti(Serial5, IMU_BAUDRATE);


// ─── Constructor / begin ──────────────────────────────────────────────────────

MTi670::MTi670(HardwareSerial& serial, uint32_t baud)
    : _serial(serial), _baud(baud) {}

void MTi670::begin()
{
    _serial.begin(_baud);
    delay(100);
    while (_serial.available()) _serial.read();  // flush

    for (int i = 0; i < 5; i++) {
        sendMsg(MID_GOTOMEASURE);
        delay(200);
    }
}

// ─── update() ────────────────────────────────────────────────────────────────

bool MTi670::update()
{
    _newData = false;
    while (_serial.available()) {
        feedByte((uint8_t)_serial.read());
    }
    return _newData;
}

// ─── XBUS send ────────────────────────────────────────────────────────────────

void MTi670::sendMsg(uint8_t mid)
{
    uint8_t msg[5];
    msg[0] = XBUS_PREAMBLE;
    msg[1] = XBUS_BID;
    msg[2] = mid;
    msg[3] = 0x00;
    msg[4] = checksum(&msg[2], 2);
    _serial.write(msg, 5);
}

uint8_t MTi670::checksum(const uint8_t* data, int len) const
{
    uint8_t sum = 0;
    for (int i = 0; i < len; i++) sum += data[i];
    return (uint8_t)(0x100 - sum);
}

// ─── Machine d'états XBUS ────────────────────────────────────────────────────

void MTi670::feedByte(uint8_t b)
{
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
            if (sum == 0x00) processPacket();
            _state = State::WAIT_PRE;
            break;
        }
    }
}

// ─── Dispatch paquet ──────────────────────────────────────────────────────────

void MTi670::processPacket()
{
    switch (_mid) {
        case MID_WAKEUP:
            sendMsg(MID_GOTOMEASURE);
            break;
        case MID_MTDATA2:
            parseMTData2(&_pkt[4], _len);
            _newData = true;
            break;
        case MID_ERROR:
            break;
        default:
            sendMsg(MID_GOTOMEASURE);
            break;
    }
}

// ─── Parseur MTData2 / XDA ───────────────────────────────────────────────────

void MTi670::parseMTData2(uint8_t* p, uint8_t plen)
{
    int i = 0;
    while (i + 2 < plen) {
        uint16_t id  = ((uint16_t)p[i] << 8) | p[i + 1];
        uint8_t  len = p[i + 2];
        uint8_t* d   = &p[i + 3];
        i += 3 + len;

        switch (id) {
            case XDA_EULER_ANGLES:
                if (len >= 12) {
                    _roll      = beFloat(d);
                    _pitch     = beFloat(d + 4);
                    _yaw       = beFloat(d + 8);
                    _att_valid = true;
                }
                break;

            case XDA_LAT_LON:
                if (len >= 16) {
                    _lat       = beDouble(d);
                    _lon       = beDouble(d + 8);
                    _pos_valid = true;
                }
                break;

            case XDA_VELOCITY_XYZ:
                if (len >= 12) {
                    _vx        = beFloat(d);
                    _vy        = beFloat(d + 4);
                    _vz        = beFloat(d + 8);
                    _speed     = sqrtf(_vx * _vx + _vy * _vy);
                    _vel_valid = true;
                }
                break;
        }
    }
}

// ─── Helpers big-endian ───────────────────────────────────────────────────────

float MTi670::beFloat(const uint8_t* p)
{
    uint32_t u = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                 ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
    float f; memcpy(&f, &u, 4); return f;
}

double MTi670::beDouble(const uint8_t* p)
{
    uint64_t u = ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
                 ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
                 ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
                 ((uint64_t)p[6] <<  8) |  (uint64_t)p[7];
    double d; memcpy(&d, &u, 8); return d;
}


// =====================================================
// Task_IMU — priorité 3, période 10ms (100Hz)
//
// Task_IMU est friend de MTi670 → accès direct aux
// membres privés _roll, _pitch, etc. sans passer
// par des méthodes publiques
// =====================================================
void Task_IMU(void *ptr)
{
    (void) ptr;
    TickType_t lastWakeTime = xTaskGetTickCount();

    s_mti.begin();

    while (1)
    {
        uint32_t t_debut = micros();

        bool nouveau_paquet = s_mti.update();

        float duree_us = (float)(micros() - t_debut);

        if (nouveau_paquet)
        {
            if (xSemaphoreTake(dataMutex, portMAX_DELAY))
            {
                // Accès direct aux membres privés via friend
                g_imu.att_valid = s_mti._att_valid;
                g_imu.vel_valid = s_mti._vel_valid;
                g_imu.pos_valid = s_mti._pos_valid;

                if (s_mti._att_valid) {
                    g_imu.roll  = s_mti._roll;
                    g_imu.pitch = s_mti._pitch;
                    g_imu.yaw   = s_mti._yaw;
                }
                if (s_mti._vel_valid) {
                    g_imu.vx    = s_mti._vx;
                    g_imu.vy    = s_mti._vy;
                    g_imu.vz    = s_mti._vz;
                    g_imu.speed = s_mti._speed;
                }
                if (s_mti._pos_valid) {
                    g_imu.lat = s_mti._lat;
                    g_imu.lon = s_mti._lon;
                }

                g_imu.temps_us = duree_us;
                xSemaphoreGive(dataMutex);
            }
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_IMU_MS));
    }
}