#include "task_xsens.h"
#include "task_watchdog.h"
#include <cmath>

extern SemaphoreHandle_t dataMutex;
static MTi670 s_mti(Serial5, BAUD_Xsens);

XsensData Xsens_data = {
    .roll = 0.0f, .pitch = 0.0f, .yaw = 0.0f, .att_valid = false,
    .lat = 0.0,   .lon = 0.0,
    .altitude = 0.0f, .pos_valid = false, .alt_valid = false,
    .vx = 0.0f, .vy = 0.0f, .vz = 0.0f, .speed = 0.0f, .vel_valid = false,
    .temps_us = 0.0f,
    .t_ms = 0,
    .v_rejets = 0, .pos_rejets = 0
};
float Xsens_temps_us = 0.0f;

// Mémoire ajoutée au buffer RX de Serial5 (64 octets par défaut) :
// à 115200 bauds, ~115 octets arrivent par période de 10 ms. Un
// retard de la tâche faisait déborder le buffer → octets perdus →
// paquets corrompus (vitesse 4.9e17 km/h dans les logs du 30/09).
static uint8_t s_rxBuffer[XSENS_RX_BUFFER];


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
        if (i + 3 + len > plen) break;   // champ tronqué : on lirait hors du paquet
        i += 3 + len;

        switch (id & XDA_MASQUE) {
            case XDA_EULER_ANGLES:
                if (len == 12) {
                    _roll      = beFloat(d);
                    _pitch     = beFloat(d + 4);
                    _yaw       = beFloat(d + 8);
                    _att_valid = true;
                }
                break;
            case XDA_LAT_LON:
                if (len == 16) {                 // Float64
                    _lat       = beDouble(d);
                    _lon       = beDouble(d + 8);
                    _pos_valid = true;
                } else if (len == 8) {           // Float32
                    _lat       = beFloat(d);
                    _lon       = beFloat(d + 4);
                    _pos_valid = true;
                }
                break;
            case XDA_ALT:
                if (len == 8) {                  // Float64
                    _altitude  = (float)beDouble(d);
                    _alt_valid = true;
                } else if (len == 4) {           // Float32
                    _altitude  = beFloat(d);
                    _alt_valid = true;
                }
                break;
            case XDA_VELOCITY_XYZ:
                if (len == 12) {
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

// =====================================================
//  Filtre de plausibilité GPS (vitesse + position)
//  Une valeur est rejetée si non finie, hors bornes, ou si elle
//  saute plus que ce qu'un bateau peut faire depuis la dernière
//  valeur acceptée. Après GPS_REJETS_RESYNC rejets de suite, on
//  accepte (vrai changement, ex. premier fix ou longue coupure).
//  Puis médiane sur 3 valeurs acceptées pour la vitesse.
// =====================================================
struct FiltreGps {
    bool     v_init = false;
    float    v_kmh  = 0.0f;       // dernière vitesse acceptée
    uint32_t v_ms   = 0;
    uint16_t v_rej_suite = 0;
    float    hist[3] = {0, 0, 0};
    uint8_t  n_hist = 0;

    bool     p_init = false;
    double   lat = 0.0, lon = 0.0;
    uint16_t p_rej_suite = 0;
};

static FiltreGps s_fgps;

static float mediane3(float a, float b, float c)
{
    if (a > b) { float t = a; a = b; b = t; }
    if (b > c) { b = c; }
    return (a > b) ? a : b;
}

// Retourne true si la vitesse est acceptée ; *v_filt = vitesse filtrée (m/s)
static bool filtreVitesse(float vx, float vy, float vz, float speed_ms, uint32_t now, float *v_filt)
{
    FiltreGps &f = s_fgps;
    float v_kmh = speed_ms * 3.6f;

    bool ok = std::isfinite(vx) && std::isfinite(vy) && std::isfinite(vz) && std::isfinite(speed_ms)
              && v_kmh >= 0.0f && v_kmh <= GPS_VITESSE_MAX_KMH
              && fabsf(vz) * 3.6f <= GPS_VITESSE_MAX_KMH;

    if (ok && f.v_init) {
        float dt_s  = (float)(now - f.v_ms) / 1000.0f;
        float saut  = GPS_SAUT_MIN_KMH + GPS_ACCEL_MAX_KMH_S * dt_s;
        if (fabsf(v_kmh - f.v_kmh) > saut && f.v_rej_suite < GPS_REJETS_RESYNC) ok = false;
    }
    // valeur non finie / hors bornes : jamais acceptée, même en re-synchro

    if (!ok) {
        if (f.v_rej_suite < 0xFFFF) f.v_rej_suite++;
        return false;
    }

    f.v_init      = true;
    f.v_kmh       = v_kmh;
    f.v_ms        = now;
    f.v_rej_suite = 0;

    f.hist[0] = f.hist[1];
    f.hist[1] = f.hist[2];
    f.hist[2] = speed_ms;
    if (f.n_hist < 3) f.n_hist++;
    *v_filt = (f.n_hist < 3) ? speed_ms : mediane3(f.hist[0], f.hist[1], f.hist[2]);
    return true;
}

static bool filtrePosition(double lat, double lon)
{
    FiltreGps &f = s_fgps;
    bool ok = std::isfinite(lat) && std::isfinite(lon) &&
              fabs(lat) <= 90.0 && fabs(lon) <= 180.0 &&
              !(lat == 0.0 && lon == 0.0);

    if (ok && f.p_init && f.p_rej_suite < GPS_REJETS_RESYNC) {
        double dn = (lat - f.lat) * 111320.0;
        double de = (lon - f.lon) * 111320.0 * cos(f.lat * 0.017453292519943295);
        if (sqrt(dn * dn + de * de) > GPS_POS_SAUT_MAX_M) ok = false;
    }

    if (!ok) {
        if (f.p_rej_suite < 0xFFFF) f.p_rej_suite++;
        return false;
    }
    f.p_init = true;
    f.lat = lat;
    f.lon = lon;
    f.p_rej_suite = 0;
    return true;
}

void Task_Xsens(void *ptr)
{
    (void) ptr;

    // Buffer RX agrandi (Serial5 = port de l'Xsens, voir s_rxBuffer)
    Serial5.addMemoryForRead(s_rxBuffer, sizeof(s_rxBuffer));
    s_mti.begin();

    const int MAX_INIT_TRIES = 10;  // 10 × 500ms = 5s max

    for (int i = 0; i < MAX_INIT_TRIES; i++)
    {
        s_mti.update();
        wd_vivant(T_XSENS);
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
                    if (filtrePosition(s_mti._lat, s_mti._lon)) {
                        Xsens_data.lat = s_mti._lat;
                        Xsens_data.lon = s_mti._lon;
                    } else {
                        Xsens_data.pos_rejets++;   // on garde la dernière position plausible
                    }
                }
                if (s_mti._alt_valid && std::isfinite(s_mti._altitude)) {
                    Xsens_data.altitude = s_mti._altitude;
                }

                Xsens_data.vel_valid = s_mti._vel_valid;
                if (s_mti._vel_valid) {
                    float v_filt;
                    if (filtreVitesse(s_mti._vx, s_mti._vy, s_mti._vz, s_mti._speed, millis(), &v_filt)) {
                        Xsens_data.vx    = s_mti._vx;
                        Xsens_data.vy    = s_mti._vy;
                        Xsens_data.vz    = s_mti._vz;
                        Xsens_data.speed = v_filt;
                    } else {
                        Xsens_data.v_rejets++;     // on garde la dernière vitesse plausible
                    }
                }

                Xsens_data.temps_us = duree_us;
                Xsens_data.t_ms     = millis();
                Xsens_temps_us      = duree_us;

                xSemaphoreGive(dataMutex);
            }
        }

        wd_vivant(T_XSENS);
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_Xsens_MS));
    }
}