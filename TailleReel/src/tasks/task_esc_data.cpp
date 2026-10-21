// =====================================================
//  task_esc_data.cpp
//  Tâche FreeRTOS — données VESC (CAN3), 200 Hz.
//  Bus, IDs et décodage repris tels quels de Real_V1.
//  Ajouts : consigne eRPM / duty / courant (MODE_COMMANDE) et
//  statistiques de qualité du bus (trames rejetées, erreurs).
// =====================================================

// ⚠ En PREMIER : FlexCAN_T4 a besoin des macros Arduino
//   que arduino_freertos.h retire ensuite.
#include <FlexCAN_T4.h>
#include "VescCAN_Teensy.h"

#include "task_esc_data.h"
#include "task_watchdog.h"

// CAN3 sur Teensy 4.1 = pin 30 (CRX3) / pin 31 (CTX3)
static FlexCAN_T4<CAN3, RX_SIZE_256, TX_SIZE_16> rawCan1;

// =====================================================
//  Adaptateur qui compte les trames avant de les donner à
//  VescCANBus (la librairie n'est pas modifiée).
//   - perdue     : mailbox écrasée avant lecture (overrun)
//   - mal formée : trame standard, ou STATUS de longueur != 8
//                  → JETÉE (le décodeur lirait des octets faux)
//   - inconnue   : commande non décodée ou ID ≠ 10/11 (transmise)
// =====================================================
// Écrits sous canMutex (lecture : cette tâche, écriture : Propulsion) ;
// lus par cette tâche (lecture 32 bits atomique).
static uint32_t cRx = 0, cMal = 0, cPerdues = 0, cInconnues = 0;
static uint32_t cTx = 0, cTxEchecs = 0;

static bool estStatus(uint8_t cmd)
{
    return cmd == CAN_PACKET_STATUS   || cmd == CAN_PACKET_STATUS_2 ||
           cmd == CAN_PACKET_STATUS_3 || cmd == CAN_PACKET_STATUS_4 ||
           cmd == CAN_PACKET_STATUS_5;
}

class CompteurAdapter : public IFlexCANAdapter {
public:
    bool read(CAN_message_t &msg) override
    {
        CAN_message_t m;
        while (rawCan1.read(m)) {
            cRx++;
            if (m.flags.overrun) cPerdues++;          // au moins une trame écrasée

            if (!m.flags.extended) { cMal++; continue; }
            uint8_t id  = (uint8_t)(m.id & 0xFF);
            uint8_t cmd = (uint8_t)((m.id >> 8) & 0xFF);
            if (estStatus(cmd)) {
                if (m.len != 8) { cMal++; continue; }
                if (id != VESC_ID_A && id != VESC_ID_B) cInconnues++;
            } else {
                cInconnues++;
            }
            msg = m;
            return true;
        }
        return false;
    }
    bool write(const CAN_message_t &msg) override
    {
        cTx++;
        bool ok = rawCan1.write(msg);
        if (!ok) cTxEchecs++;
        return ok;
    }
};

static CompteurAdapter adapterCan1;
static VescCANBus      vesc(adapterCan1);

// Accès au périphérique CAN : lecture (cette tâche) + écriture (Propulsion)
static SemaphoreHandle_t canMutex = nullptr;

static const uint8_t VESC_IDS[NB_VESC] = {VESC_ID_A, VESC_ID_B};

// ─── Définition des variables partagées ───
EscData  ESC_data[NB_VESC] = {};
CanStats ESC_can           = {};
float    ESC_temps_us      = 0.0f;

void ESC_InitCAN()
{
    canMutex = xSemaphoreCreateMutex();
    rawCan1.begin();
    rawCan1.setBaudRate(BAUD_CAN);

    // Mailboxes : 32 au total, 0-15 en RX étendues (trames des VESC),
    // 16-31 en TX. C'est la disposition qui recevait bien au lac ; la
    // disposition par défaut de FlexCAN_T4 (4 RX étendues, 4-7) ne
    // reçoit rien sur ce montage.
    //  1) setMaxMB(32)     : passe à 32 mailboxes. ATTENTION : il
    //                        réinitialise AVANT de changer le nombre, les
    //                        mailboxes 16-31 restent non initialisées...
    //  2) enableFIFO(false): ...donc on relance la disposition par défaut
    //                        sur les 32 : tout est effacé, 0-15 RX
    //                        (0-7 std, 8-15 étendues), 16-31 TX valides.
    //  3) setMB(0..7)      : 0-7 passent aussi en RX étendues.
    rawCan1.setMaxMB(32);
    rawCan1.enableFIFO(false);
    for (int i = 0; i < 8; i++) {
        rawCan1.setMB((FLEXCAN_MAILBOX)i, RX, EXT);
    }

    vesc.begin();
}

// Envoi d'une consigne à un VESC selon MODE_COMMANDE.
// v : eRPM / ‰ de duty / 0,1 A
static void envoyer(uint8_t id, int32_t v)
{
#if MODE_COMMANDE == MODE_ERPM
    vesc.setERPM(id, v);
#elif MODE_COMMANDE == MODE_DUTY
    if (v == 0) vesc.setCurrent(id, 0.0f);          // relâche le moteur (roue libre)
    else        vesc.setDuty(id, (float)v / 1000.0f);
#else // MODE_COURANT
    vesc.setCurrent(id, (float)v / 10.0f);          // 0 = moteur relâché
#endif
}

void ESC_EnvoyerConsigne(int32_t cmd_a, int32_t cmd_b)
{
    if (xSemaphoreTake(canMutex, portMAX_DELAY))
    {
        envoyer(VESC_ID_A, cmd_a);
        envoyer(VESC_ID_B, cmd_b);
        xSemaphoreGive(canMutex);
    }
}

// Real_V1 : vescVivant()
static bool vescVivant(uint8_t id)
{
    return vesc.isUpdated(id) &&
           (millis() - vesc.lastUpdateMs(id) < VESC_TIMEOUT_MS);
}

// =====================================================
//  Task_ESC_Data — 200 Hz
// =====================================================
void Task_ESC_Data(void *ptr)
{
    (void) ptr;

    // Statistiques : erreurs matérielles (ESR1 relu à chaque cycle,
    // ses bits d'erreur s'effacent à la lecture → on compte les
    // cycles de 5 ms où au moins une erreur a été vue).
    const uint32_t ERR_BUS = FLEXCAN_ESR_STF_ERR | FLEXCAN_ESR_FRM_ERR | FLEXCAN_ESR_CRC_ERR |
                             FLEXCAN_ESR_BIT0_ERR | FLEXCAN_ESR_BIT1_ERR;
    uint32_t errBus = 0, errAck = 0;
    uint8_t  rec = 0, tec = 0, flt = 0;

    // Fenêtre du pourcentage récent
    uint32_t fenDebut = millis();
    uint32_t fenRx0 = 0, fenBad0 = 0;
    float    pctFen = 0.0f;

    TickType_t lastWakeTime = xTaskGetTickCount();

    while (1)
    {
        uint32_t t_debut = micros();

        // Lecture CAN — toujours, sinon le buffer déborde
        if (xSemaphoreTake(canMutex, portMAX_DELAY))
        {
            vesc.update();
            uint32_t esr = FLEXCANb_ESR1(CAN3);
            uint32_t ecr = FLEXCANb_ECR(CAN3);
            xSemaphoreGive(canMutex);

            if (esr & ERR_BUS)             errBus++;
            if (esr & FLEXCAN_ESR_ACK_ERR) errAck++;   // personne n'acquitte (ESC éteints)
            tec = (uint8_t)(ecr & 0xFF);
            rec = (uint8_t)((ecr >> 8) & 0xFF);
            flt = (uint8_t)((esr & FLEXCAN_ESR_FLT_CONF_MASK) >> 4);   // 0 actif, 1 passif, 2-3 bus off
        }

        // Copie locale (vesc._data n'est écrit que par cette tâche)
        EscData d[NB_VESC];
        for (int k = 0; k < NB_VESC; k++)
        {
            uint8_t id  = VESC_IDS[k];
            d[k].id      = id;
            d[k].vivant  = vescVivant(id);
            d[k].deja_vu = vesc.isUpdated(id);
            d[k].erpm    = vesc.getERPM(id);
            d[k].duty    = vesc.getDutyCycle(id);
            d[k].i_mot   = vesc.getMotorCurrent(id);
            d[k].i_in    = vesc.getCurrentIn(id);
            d[k].v_in    = vesc.getVoltageIn(id);
            d[k].t_fet   = vesc.getTempFET(id);
            d[k].t_mot   = vesc.getTempMotor(id);
        }

        // Statistiques CAN
        CanStats cs;
        cs.rx        = cRx;
        cs.mal       = cMal;
        cs.perdues   = cPerdues;
        cs.inconnues = cInconnues;
        cs.tx        = cTx;
        cs.tx_echecs = cTxEchecs;
        cs.err_bus   = errBus;
        cs.err_ack   = errAck;
        cs.rec       = rec;
        cs.tec       = tec;
        cs.flt       = flt;

        uint32_t bad   = cs.mal + cs.perdues;
        uint32_t total = cs.rx + cs.perdues;            // une trame perdue n'a jamais été lue
        cs.pct_total   = total ? 100.0f * (float)bad / (float)total : 0.0f;

        uint32_t now = millis();
        if (now - fenDebut >= CAN_STATS_FENETRE_MS) {
            uint32_t dRx  = (cs.rx + cs.perdues) - fenRx0;
            uint32_t dBad = bad - fenBad0;
            pctFen  = dRx ? 100.0f * (float)dBad / (float)dRx : 0.0f;
            fenRx0  = cs.rx + cs.perdues;
            fenBad0 = bad;
            fenDebut = now;
        }
        cs.pct_fenetre = pctFen;

        float duree_us = (float)(micros() - t_debut);

        if (xSemaphoreTake(dataMutex, portMAX_DELAY))
        {
            for (int k = 0; k < NB_VESC; k++) ESC_data[k] = d[k];
            ESC_can      = cs;
            ESC_temps_us = duree_us;
            xSemaphoreGive(dataMutex);
        }

        wd_vivant(T_ESC);
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(PERIODE_ESC_MS));
    }
}
