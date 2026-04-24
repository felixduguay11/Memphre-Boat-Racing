#!/usr/bin/env python3
"""
vesc_uart.py — Bibliothèque Python VESC UART pour Raspberry Pi 4
Traduction directe de VescUart.cpp / buffer.cpp / crc.cpp en Python.
Communique avec le VESC via le protocole binaire natif (pas Teleplot).

Connexion recommandée :
    VESC TX  →  Pi GPIO15 (RXD)   ou  adaptateur USB-UART
    VESC RX  →  Pi GPIO14 (TXD)
    GND      →  Pi GND

Port série :
    GPIO UART  →  /dev/ttyAMA0   (après avoir désactivé le Bluetooth : dtoverlay=disable-bt)
    USB-UART   →  /dev/ttyUSB0   ou  /dev/ttyACM0
"""

import struct
import serial
import time


# ─────────────────────────────────────────────────────────────────────────────
# CRC-16 (CCITT) — traduction de crc.cpp
# ─────────────────────────────────────────────────────────────────────────────
CRC16_TABLE = [
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
    0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
    0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
    0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
    0x4864, 0x5845, 0x6826, 0x7807, 0x08e0, 0x18c1, 0x28a2, 0x3883,
    0xc96c, 0xd94d, 0xe92e, 0xf90f, 0x89e8, 0x99c9, 0xa9aa, 0xb98b,
    0x5a55, 0x4a74, 0x7a17, 0x6a36, 0x1ad1, 0x0af0, 0x3a93, 0x2ab2,
    0xdb5d, 0xcb7c, 0xfb1f, 0xeb3e, 0x9bd9, 0x8bf8, 0xbb9b, 0xabba,
    0x6c66, 0x7c47, 0x4c24, 0x5c05, 0x2ce2, 0x3cc3, 0x0ca0, 0x1c81,
    0xed6e, 0xfd4f, 0xcd2c, 0xdd0d, 0xad6a, 0xbd4b, 0x8d28, 0x9d09,
    0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
    0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
    0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
    0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
    0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
    0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
    0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
    0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
    0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
    0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
    0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
    0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
    0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0,
]

def crc16(data: bytes) -> int:
    crc = 0
    for b in data:
        crc = ((crc << 8) ^ CRC16_TABLE[((crc >> 8) ^ b) & 0xFF]) & 0xFFFF
    return crc


# ─────────────────────────────────────────────────────────────────────────────
# IDs de commandes VESC (datatypes.h → COMM_PACKET_ID)
# ─────────────────────────────────────────────────────────────────────────────
COMM_FW_VERSION      = 0
COMM_GET_VALUES      = 4
COMM_SET_DUTY        = 5
COMM_SET_CURRENT     = 6
COMM_SET_CURRENT_BRAKE = 7
COMM_SET_RPM         = 8
COMM_SET_POS         = 9
COMM_SET_CHUCK_DATA  = 24
COMM_FORWARD_CAN     = 33
COMM_ALIVE           = 30


# ─────────────────────────────────────────────────────────────────────────────
# Structure de données VESC (équivalent de struct bldcMeasure dans datatypes.h)
# ─────────────────────────────────────────────────────────────────────────────
class VescData:
    def __init__(self):
        self.tempMosfet       = 0.0
        self.tempMotor        = 0.0
        self.avgMotorCurrent  = 0.0
        self.avgInputCurrent  = 0.0
        self.dutyCycleNow     = 0.0
        self.rpm              = 0.0
        self.inpVoltage       = 0.0
        self.ampHours         = 0.0
        self.ampHoursCharged  = 0.0
        self.wattHours        = 0.0
        self.wattHoursCharged = 0.0
        self.tachometer       = 0
        self.tachometerAbs    = 0
        self.error            = 0
        self.pidPos           = 0.0
        self.id               = 0


# ─────────────────────────────────────────────────────────────────────────────
# Fonctions buffer (buffer.cpp → Python)
# ─────────────────────────────────────────────────────────────────────────────
def _get_float16(data: bytes, scale: float, idx: int) -> tuple:
    val = struct.unpack_from('>h', data, idx)[0]  # int16 big-endian signé
    return val / scale, idx + 2

def _get_float32(data: bytes, scale: float, idx: int) -> tuple:
    val = struct.unpack_from('>i', data, idx)[0]  # int32 big-endian signé
    return val / scale, idx + 4

def _get_int32(data: bytes, idx: int) -> tuple:
    val = struct.unpack_from('>i', data, idx)[0]
    return val, idx + 4

def _append_int32(val: int) -> bytes:
    return struct.pack('>i', int(val))


# ─────────────────────────────────────────────────────────────────────────────
# Classe principale VescUart
# ─────────────────────────────────────────────────────────────────────────────
class VescUart:
    """
    Interface UART avec un VESC.

    Exemple d'utilisation :
        uart = VescUart('/dev/ttyAMA0', baudrate=115200)
        uart.connect()
        if uart.get_values():
            print(uart.data.rpm)
        uart.set_rpm(3000)
        uart.disconnect()
    """

    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 0.1):
        self.port     = port
        self.baudrate = baudrate
        self.timeout  = timeout
        self._ser     = None
        self.data     = VescData()

    # ── Connexion / déconnexion ───────────────────────────────────────────────
    def connect(self):
        self._ser = serial.Serial(self.port, self.baudrate, timeout=self.timeout)
        time.sleep(0.05)

    def disconnect(self):
        if self._ser and self._ser.is_open:
            self._ser.close()

    @property
    def is_connected(self) -> bool:
        return self._ser is not None and self._ser.is_open

    # ── Couche trame (équivalent de packSendPayload / receiveUartMessage) ─────
    def _pack_and_send(self, payload: bytes):
        """Encapsule un payload avec entête de longueur + CRC + stop byte."""
        ln = len(payload)
        crc = crc16(payload)
        if ln <= 255:
            frame = bytes([2, ln]) + payload + bytes([(crc >> 8) & 0xFF, crc & 0xFF, 3])
        else:
            frame = bytes([3, (ln >> 8) & 0xFF, ln & 0xFF]) + payload + \
                    bytes([(crc >> 8) & 0xFF, crc & 0xFF, 3])
        self._ser.write(frame)

    def _receive_message(self, max_bytes: int = 512) -> bytes:
        """Lit une trame VESC complète et retourne le payload (sans en-tête ni CRC)."""
        buf = b''
        deadline = time.time() + self.timeout * 10

        while time.time() < deadline:
            waiting = self._ser.in_waiting
            if waiting:
                buf += self._ser.read(waiting)
            if len(buf) >= 2:
                start = buf[0]
                if start == 2 and len(buf) >= 2:
                    ln = buf[1]
                    needed = ln + 5          # 2 header + payload + 2 CRC + 1 stop
                    if len(buf) >= needed and buf[needed - 1] == 3:
                        payload = buf[2:2 + ln]
                        crc_recv = (buf[2 + ln] << 8) | buf[3 + ln]
                        if crc16(payload) == crc_recv:
                            return payload
                        return b''
                elif start == 3 and len(buf) >= 3:
                    ln = (buf[1] << 8) | buf[2]
                    needed = ln + 6
                    if len(buf) >= needed and buf[needed - 1] == 3:
                        payload = buf[3:3 + ln]
                        crc_recv = (buf[3 + ln] << 8) | buf[4 + ln]
                        if crc16(payload) == crc_recv:
                            return payload
                        return b''
            time.sleep(0.005)
        return b''

    # ── Lecture des valeurs VESC (COMM_GET_VALUES) ────────────────────────────
    def get_values(self, can_id: int = 0) -> bool:
        """Lit toutes les mesures du VESC. Retourne True si succès."""
        if can_id:
            payload = bytes([COMM_FORWARD_CAN, can_id, COMM_GET_VALUES])
        else:
            payload = bytes([COMM_GET_VALUES])

        self._pack_and_send(payload)
        msg = self._receive_message()
        if not msg:
            return False

        packet_id = msg[0]
        if packet_id != COMM_GET_VALUES:
            return False

        d = msg[1:]
        idx = 0
        self.data.tempMosfet,       idx = _get_float16(d, 10.0,      idx)
        self.data.tempMotor,         idx = _get_float16(d, 10.0,      idx)
        self.data.avgMotorCurrent,   idx = _get_float32(d, 100.0,     idx)
        self.data.avgInputCurrent,   idx = _get_float32(d, 100.0,     idx)
        idx += 4   # skip id_current
        idx += 4   # skip iq_current
        self.data.dutyCycleNow,      idx = _get_float16(d, 1000.0,    idx)
        self.data.rpm,               idx = _get_float32(d, 1.0,       idx)
        self.data.inpVoltage,        idx = _get_float16(d, 10.0,      idx)
        self.data.ampHours,          idx = _get_float32(d, 10000.0,   idx)
        self.data.ampHoursCharged,   idx = _get_float32(d, 10000.0,   idx)
        self.data.wattHours,         idx = _get_float32(d, 10000.0,   idx)
        self.data.wattHoursCharged,  idx = _get_float32(d, 10000.0,   idx)
        self.data.tachometer,        idx = _get_int32(d, idx)
        self.data.tachometerAbs,     idx = _get_int32(d, idx)
        self.data.error              = d[idx]; idx += 1
        self.data.pidPos,            idx = _get_float32(d, 1000000.0, idx)
        if idx < len(d):
            self.data.id = d[idx]
        return True

    # ── Commandes de contrôle ─────────────────────────────────────────────────
    def set_current(self, current_a: float, can_id: int = 0):
        """Commande en courant (Ampères, positif = moteur, négatif = freinage regen)."""
        val = int(current_a * 1000)
        if can_id:
            self._pack_and_send(bytes([COMM_FORWARD_CAN, can_id, COMM_SET_CURRENT]) + _append_int32(val))
        else:
            self._pack_and_send(bytes([COMM_SET_CURRENT]) + _append_int32(val))

    def set_brake_current(self, current_a: float, can_id: int = 0):
        """Commande en courant de freinage (Ampères, toujours positif)."""
        val = int(current_a * 1000)
        if can_id:
            self._pack_and_send(bytes([COMM_FORWARD_CAN, can_id, COMM_SET_CURRENT_BRAKE]) + _append_int32(val))
        else:
            self._pack_and_send(bytes([COMM_SET_CURRENT_BRAKE]) + _append_int32(val))

    def set_rpm(self, rpm: float, can_id: int = 0):
        """Commande en RPM."""
        val = int(rpm)
        if can_id:
            self._pack_and_send(bytes([COMM_FORWARD_CAN, can_id, COMM_SET_RPM]) + _append_int32(val))
        else:
            self._pack_and_send(bytes([COMM_SET_RPM]) + _append_int32(val))

    def set_duty(self, duty: float, can_id: int = 0):
        """Commande en duty cycle (-1.0 à 1.0)."""
        val = int(duty * 100000)
        if can_id:
            self._pack_and_send(bytes([COMM_FORWARD_CAN, can_id, COMM_SET_DUTY]) + _append_int32(val))
        else:
            self._pack_and_send(bytes([COMM_SET_DUTY]) + _append_int32(val))

    def send_keepalive(self, can_id: int = 0):
        """Envoie un keepalive pour éviter le timeout de sécurité du VESC."""
        if can_id:
            self._pack_and_send(bytes([COMM_FORWARD_CAN, can_id, COMM_ALIVE]))
        else:
            self._pack_and_send(bytes([COMM_ALIVE]))
