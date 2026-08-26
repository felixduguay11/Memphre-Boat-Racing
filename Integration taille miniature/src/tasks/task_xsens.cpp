#include "task_xsens.h"

extern SemaphoreHandle_t dataMutex;
static MTi670 s_mti(Serial4, BAUD_Xsens);

XsensData Xsens_data = {
    .roll = 0.0f, .pitch = 0.0f, .yaw = 0.0f, .att_valid = false,
    .lat = 0.0,   .lon = 0.0,
    .altitude = 0.0f, .pos_valid = false, .alt_valid = false,
    .vx = 0.0f, .vy = 0.0f, .vz = 0.0f, .speed = 0.0f, .vel_valid = false,
    .temps_us = 0.0f
};
float Xsens_temps_us = 0.0f;


MTi670::MTi670(HardwareSerial& serial, uint32_t baud)
    : _serial(serial), _baud(baud) {}

void MTi670::begin()
{
    _serial.begin(_baud);

    // Flush du buffer UART — limité pour éviter blocage
    int flushCount = 0;
    while (_serial.available() && flushCount < 256) {
        _serial.read();
        flushCount++;
    }
}

void MTi670::requestMeasurement()
{
    sendMsg(MID_GOTOMEASURE);
}

bool MTi670::update()
{
    _newData = false;

    int count = 0;
    while (_serial.available() && count < XSENS_MAX_BYTES_PER_UPDATE) {
        feedByte((uint8_t)_serial.read());
        count++;
    }
    return _newData;
}

void MTi670::sendMsg(uint8_t mid)
{
    uint8_t msg[5];
    msg[0] = XBUS_PREAMBLE;
    msg[1] = XBUS_BID;
    msg[2] = mid;
    msg[3] = 0x00;
    msg[4] = checksum(&msg[1], 3);
    _serial.write(msg, 5);
}

uint8_t MTi670::checksum(const uint8_t* data, int len) const
{
    uint8_t sum = 0;
    for (int i = 0; i < len; i++) sum += data[i];
    return (uint8_t)(0x100 - sum);
}

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

void MTi670::processPacket()
{
    switch (_mid) {
        case MID_WAKEUP:
            _gotWakeUp = true;
            sendMsg(MID_GOTOMEASURE);
            break;
        case MID_GOTOMEASURE_ACK:
            break;
        case MID_MTDATA2:
            parseMTData2(&_pkt[4], _len);
            _newData = true;
            break;
        case 0x31:
            sendMsg(MID_GOTOMEASURE);
            break;
        case MID_ERROR:
            break;
        default:
            break;
    }
}

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
            case XDA_ALT:
                if (len >= 4) {
                    _altitude  = beFloat(d);
                    _alt_valid = true;
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

void Task_Xsens(void *ptr)
{
    (void) ptr;

    s_mti.begin();

    const int MAX_INIT_TRIES = 10;  // 10 × 500ms = 5s max

    for (int i = 0; i < MAX_INIT_TRIES; i++)
    {
        s_mti.update();
        if (s_mti.isReady()) break;

        s_mti.requestMeasurement();
        vTaskDelay(pdMS_TO_TICKS(500));  // ← libère le CPU
    }

    // --- Boucle principale ---
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (1)
    {
        uint32_t t_debut = micros();

        bool nouveau_paquet = s_mti.update();

        float duree_us = (float)(micros() - t_debut);

        if (nouveau_paquet)
        {
            if (xSemaphoreTake(dataMutex, portMAX_DELAY))
            {
                Xsens_data.att_valid = s_mti._att_valid;
                if (s_mti._att_valid) {
                    Xsens_data.roll  = s_mti._roll;
                    Xsens_data.pitch = s_mti._pitch;
                    Xsens_data.yaw   = s_mti._yaw;
                }

                Xsens_data.pos_valid = s_mti._pos_valid;
                Xsens_data.alt_valid = s_mti._alt_valid;
                if (s_mti._pos_valid) {
                    Xsens_data.lat = s_mti._lat;
                    Xsens_data.lon = s_mti._lon;
                }
                if (s_mti._alt_valid) {
                    Xsens_data.altitude = s_mti._altitude;
                }

                Xsens_data.vel_valid = s_mti._vel_valid;
                if (s_mti._vel_valid) {
                    Xsens_data.vx    = s_mti._vx;
                    Xsens_data.vy    = s_mti._vy;
                    Xsens_data.vz    = s_mti._vz;
                    Xsens_data.speed = s_mti._speed;
                }

                Xsens_data.temps_us = duree_us;
                Xsens_temps_us      = duree_us;

                xSemaphoreGive(dataMutex);
            }
        }

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_Xsens_MS));
    }
}