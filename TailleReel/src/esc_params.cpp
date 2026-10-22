// =====================================================
//  esc_params.cpp
//  Lecture des paramètres des VESC par CAN (voir esc_params.h).
//
//  Partage entre tâches :
//   - réassemblage (rx, recu, fin*) : Task_ESC_Data seulement ;
//   - slots (configs lues)          : écrits par Task_ESC_Data,
//                                     lus par Task_RPi, sous mtx.
// =====================================================

#include "esc_params.h"
#include <semphr.h>
#include <cstring>
#include <cstdio>

#if ESC_PARAMS_ACTIF

namespace {

// Types de trames CAN « buffer » des VESC (datatypes.h, CAN_PACKET_ID)
constexpr uint8_t PKT_FILL      = 5;
constexpr uint8_t PKT_FILL_LONG = 6;
constexpr uint8_t PKT_PROCESS   = 7;
constexpr uint8_t PKT_SHORT     = 8;

// Commandes (datatypes.h, COMM_PACKET_ID)
constexpr uint8_t COMM_GET_MCCONF  = 14;
constexpr uint8_t COMM_GET_APPCONF = 17;

constexpr int NB_TYPES = 2;                       // 0 = mcconf, 1 = appconf
const uint8_t COMM_TYPE[NB_TYPES] = {COMM_GET_MCCONF, COMM_GET_APPCONF};
const char*   NOM_TYPE[NB_TYPES]  = {"mc", "app"};
const uint8_t IDS[NB_VESC]        = {VESC_ID_A, VESC_ID_B};

constexpr uint16_t RX_MAX = ESC_PARAMS_MAX_OCTETS + 1;   // + octet de commande

enum class Etat : uint8_t { RIEN, A_LIRE, EN_COURS, OK, ECHEC };
enum Erreur : uint8_t { ERR_AUCUNE, ERR_TIMEOUT, ERR_CRC, ERR_REPONSE };
const char* NOM_ERR[] = {"", "timeout", "crc", "reponse"};

struct Slot {
    Etat     etat;
    uint8_t  essais;
    uint8_t  err;
    bool     a_envoyer;        // résultat à transmettre au Pi
    uint32_t pret_ms;          // pas de requête avant cet instant
    uint32_t t_ms;             // fin de la lecture (identifie la lecture côté Pi)
    uint16_t len;              // octets de config (sans l'octet de commande)
    uint8_t  data[ESC_PARAMS_MAX_OCTETS];
};

Slot              slots[NB_VESC][NB_TYPES];
SemaphoreHandle_t mtx = nullptr;

// ─── Réassemblage (Task_ESC_Data) ───
int8_t   actifEsc  = -1;
int8_t   actifType = -1;
uint32_t actifDebut = 0;
uint8_t  rx[RX_MAX];
uint8_t  recu[(RX_MAX + 7) / 8];   // 1 bit par octet reçu (trames dans le désordre)
bool     finRecue  = false;
uint8_t  finSender = 0;
uint16_t finLen    = 0;
uint16_t finCrc    = 0;

bool    vivantAvant[NB_VESC] = {};
uint8_t tourniquet = 0;            // équité de l'envoi au Pi

// CRC16 du firmware VESC (crc.c) : CRC-16/XMODEM, poly 0x1021, init 0
uint16_t crc16(const uint8_t *d, uint16_t n)
{
    uint16_t c = 0;
    for (uint16_t i = 0; i < n; i++) {
        c ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++)
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

void marquer(uint16_t off, uint8_t n)
{
    for (uint16_t i = off; i < off + n && i < RX_MAX; i++)
        recu[i >> 3] |= (uint8_t)(1u << (i & 7));
}

bool complet(uint16_t n)
{
    for (uint16_t i = 0; i < n; i++)
        if (!(recu[i >> 3] & (1u << (i & 7)))) return false;
    return true;
}

void verrou()    { if (mtx) xSemaphoreTake(mtx, portMAX_DELAY); }
void deverrou()  { if (mtx) xSemaphoreGive(mtx); }

// Essai raté : nouvel essai plus tard, ou abandon après ESC_PARAMS_ESSAIS
void echecEssai(uint32_t now, uint8_t err)
{
    Slot &s = slots[actifEsc][actifType];
    verrou();
    s.essais++;
    if (s.essais >= ESC_PARAMS_ESSAIS) {
        s.etat      = Etat::ECHEC;
        s.err       = err;
        s.t_ms      = now;
        s.a_envoyer = true;
    } else {
        s.etat    = Etat::A_LIRE;
        s.pret_ms = now + ESC_PARAMS_PAUSE_MS;
    }
    deverrou();
    actifEsc = actifType = -1;
}

// Toutes les trames sont là : vérification et stockage
void terminer(uint32_t now)
{
    bool ok = finSender == IDS[actifEsc] &&
              finLen >= 2 && finLen <= RX_MAX &&
              rx[0] == COMM_TYPE[actifType];
    if (!ok) { echecEssai(now, ERR_REPONSE); return; }
    if (crc16(rx, finLen) != finCrc) { echecEssai(now, ERR_CRC); return; }

    Slot &s = slots[actifEsc][actifType];
    verrou();
    s.len = finLen - 1;
    memcpy(s.data, rx + 1, s.len);
    s.etat      = Etat::OK;
    s.err       = ERR_AUCUNE;
    s.t_ms      = now;
    s.a_envoyer = true;
    deverrou();
    actifEsc = actifType = -1;
}

} // namespace

void EscParams_Init()
{
    if (!mtx) mtx = xSemaphoreCreateMutex();
    memset(slots, 0, sizeof(slots));
}

bool EscParams_TraiterTrame(uint32_t eid, const uint8_t *d, uint8_t len)
{
    if ((eid & 0xFF) != TEENSY_CAN_ID) return false;
    uint8_t cmd = (uint8_t)((eid >> 8) & 0xFF);
    if (cmd < PKT_FILL || cmd > PKT_SHORT) return false;

    if (actifEsc < 0) return true;              // aucune lecture en cours : ignorée

    switch (cmd) {
    case PKT_FILL:                              // [offset 8 bits][7 octets max]
        if (len >= 2) {
            uint16_t off = d[0];
            uint8_t  n   = len - 1;
            if (off + n <= RX_MAX) { memcpy(rx + off, d + 1, n); marquer(off, n); }
        }
        break;
    case PKT_FILL_LONG:                         // [offset 16 bits][6 octets max]
        if (len >= 3) {
            uint16_t off = (uint16_t)((d[0] << 8) | d[1]);
            uint8_t  n   = len - 2;
            if (off + n <= RX_MAX) { memcpy(rx + off, d + 2, n); marquer(off, n); }
        }
        break;
    case PKT_PROCESS:                           // [expéditeur][send][len 16][CRC 16]
        if (len >= 6) {
            finSender = d[0];
            finLen    = (uint16_t)((d[2] << 8) | d[3]);
            finCrc    = (uint16_t)((d[4] << 8) | d[5]);
            finRecue  = true;
        }
        break;
    default:                                    // réponse courte : pas une config
        break;
    }
    return true;
}

bool EscParams_Cycle(uint32_t now, const bool vivant[NB_VESC],
                     uint32_t &eid, uint8_t data[8], uint8_t &len)
{
    // ─── VESC qui (re)commence à parler → relire ses deux configs ───
    for (int k = 0; k < NB_VESC; k++) {
        if (vivant[k] && !vivantAvant[k]) {
            if (actifEsc == k) actifEsc = actifType = -1;
            verrou();
            for (int t = 0; t < NB_TYPES; t++) {
                Slot &s = slots[k][t];
                s.etat      = Etat::A_LIRE;
                s.essais    = 0;
                s.err       = ERR_AUCUNE;
                s.a_envoyer = false;
                s.pret_ms   = now + ESC_PARAMS_DELAI_MS;
            }
            deverrou();
        }
        else if (!vivant[k] && vivantAvant[k] && actifEsc == k) {
            actifEsc = actifType = -1;          // VESC éteint pendant la lecture
        }
        vivantAvant[k] = vivant[k];
    }

    // ─── Lecture en cours ───
    if (actifEsc >= 0) {
        if (finRecue && finLen > RX_MAX)              echecEssai(now, ERR_REPONSE);
        else if (finRecue && complet(finLen))         terminer(now);
        else if (now - actifDebut > ESC_PARAMS_TIMEOUT_MS) echecEssai(now, ERR_TIMEOUT);
        if (actifEsc >= 0) return false;
    }

    // ─── Prochaine requête (une seule à la fois) ───
    for (int k = 0; k < NB_VESC; k++) {
        if (!vivant[k]) continue;
        for (int t = 0; t < NB_TYPES; t++) {
            Slot &s = slots[k][t];
            if (s.etat != Etat::A_LIRE || (int32_t)(now - s.pret_ms) < 0) continue;

            actifEsc   = (int8_t)k;
            actifType  = (int8_t)t;
            actifDebut = now;
            finRecue   = false;
            memset(recu, 0, sizeof(recu));
            verrou();
            s.etat = Etat::EN_COURS;
            deverrou();

            eid     = ((uint32_t)PKT_SHORT << 8) | IDS[k];
            data[0] = TEENSY_CAN_ID;    // à qui répondre
            data[1] = 0;                // 0 : commands_process_packet chez l'ESC
            data[2] = COMM_TYPE[t];
            len     = 3;
            return true;
        }
    }
    return false;
}

bool EscParams_TransfertEnCours()
{
    return actifEsc >= 0;
}

void EscParams_DemanderRenvoi()
{
    verrou();
    for (int k = 0; k < NB_VESC; k++)
        for (int t = 0; t < NB_TYPES; t++)
            if (slots[k][t].etat == Etat::OK || slots[k][t].etat == Etat::ECHEC)
                slots[k][t].a_envoyer = true;
    deverrou();
}

int EscParams_ProchaineLigne(char *buf, int max)
{
    static const char HEX[] = "0123456789abcdef";
    int n = 0;
    verrou();
    for (int i = 0; i < NB_VESC * NB_TYPES && n == 0; i++) {
        int idx = (tourniquet + i) % (NB_VESC * NB_TYPES);
        int k = idx / NB_TYPES, t = idx % NB_TYPES;
        Slot &s = slots[k][t];
        if (!s.a_envoyer) continue;
        s.a_envoyer = false;
        tourniquet  = (uint8_t)(idx + 1);

        if (s.etat == Etat::OK) {
            int h = snprintf(buf, max, "{\"type\":\"escconf\",\"id\":%u,\"k\":\"%s\",\"t\":%lu,\"ok\":1,\"len\":%u,\"hex\":\"",
                             IDS[k], NOM_TYPE[t], (unsigned long)s.t_ms, s.len);
            if (h <= 0 || h + 2 * s.len + 3 >= max) continue;   // ne rentre pas : on saute
            for (uint16_t j = 0; j < s.len; j++) {
                buf[h++] = HEX[s.data[j] >> 4];
                buf[h++] = HEX[s.data[j] & 0x0F];
            }
            buf[h++] = '"'; buf[h++] = '}'; buf[h++] = '\n'; buf[h] = '\0';
            n = h;
        } else {
            int h = snprintf(buf, max, "{\"type\":\"escconf\",\"id\":%u,\"k\":\"%s\",\"t\":%lu,\"ok\":0,\"err\":\"%s\"}\n",
                             IDS[k], NOM_TYPE[t], (unsigned long)s.t_ms, NOM_ERR[s.err < 4 ? s.err : 0]);
            if (h > 0 && h < max) n = h;
        }
    }
    deverrou();
    return n;
}

#else  // ESC_PARAMS_ACTIF == 0 : fonctions vides

void EscParams_Init() {}
bool EscParams_TraiterTrame(uint32_t, const uint8_t *, uint8_t) { return false; }
bool EscParams_Cycle(uint32_t, const bool[NB_VESC], uint32_t &, uint8_t[8], uint8_t &) { return false; }
bool EscParams_TransfertEnCours() { return false; }
void EscParams_DemanderRenvoi() {}
int  EscParams_ProchaineLigne(char *, int) { return 0; }

#endif
