#pragma once

// =====================================================
//  esc_params.h
//  Lecture des paramètres des VESC (mcconf + appconf) par CAN.
//
//  Quand un VESC se met à parler (démarrage des ESC), le Teensy
//  lui demande sa config, une fois :
//    requête  : CAN_PACKET_PROCESS_SHORT_BUFFER (8) vers l'ESC,
//               [TEENSY_CAN_ID, 0, COMM_GET_MCCONF (14) / COMM_GET_APPCONF (17)]
//    réponse  : FILL_RX_BUFFER (5) / FILL_RX_BUFFER_LONG (6) puis
//               PROCESS_RX_BUFFER (7) [id ESC, 1, len, CRC16], adressés
//               à TEENSY_CAN_ID (protocole comm_can.c, firmware 6.06).
//  Une seule lecture à la fois (les trames FILL ne portent pas
//  l'ID de l'expéditeur). 3 essais par config.
//
//  Les octets bruts sont relayés au Pi par Task_RPi :
//    {"type":"escconf","id":10,"k":"mc","t":12345,"ok":1,"len":483,"hex":"..."}
//    {"type":"escconf","id":10,"k":"app","t":12345,"ok":0,"err":"timeout"}
//  t = millis() à la fin de la lecture : le Pi s'en sert pour ne
//  pas réafficher une config simplement renvoyée.
//  Le Pi décode et compare aux fichiers de référence.
//  {"cmd":"escconf"} du Pi → renvoi des configs déjà lues.
// =====================================================

#include <arduino_freertos.h>
#include "config.h"

void EscParams_Init();

// ─── Côté Task_ESC_Data (seule propriétaire du CAN) ───

// Trame étendue reçue. Retourne true si elle est adressée au
// Teensy (TEENSY_CAN_ID) avec un type « buffer » : elle est alors
// consommée ici et ne doit pas être décodée comme un STATUS.
bool EscParams_TraiterTrame(uint32_t eid, const uint8_t *data, uint8_t len);

// À appeler à chaque cycle. vivant[k] : le VESC k (ordre
// VESC_ID_A, VESC_ID_B) envoie des trames. Retourne true si une
// requête doit partir maintenant (eid, data, len remplis).
bool EscParams_Cycle(uint32_t now, const bool vivant[NB_VESC],
                     uint32_t &eid, uint8_t data[8], uint8_t &len);

// Une réponse est en cours de réception (vider le bus plus souvent)
bool EscParams_TransfertEnCours();

// ─── Côté Task_RPi ───

// {"cmd":"escconf"} : renvoyer au Pi les configs déjà lues
void EscParams_DemanderRenvoi();

// Prochaine ligne JSON (terminée par \n) à envoyer au Pi.
// Retourne sa longueur, ou 0 s'il n'y a rien à envoyer.
int EscParams_ProchaineLigne(char *buf, int max);
