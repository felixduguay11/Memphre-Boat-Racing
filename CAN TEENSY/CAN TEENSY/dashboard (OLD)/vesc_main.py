#!/usr/bin/env python3
"""
vesc_main.py — Remplacement du fichier .ino pour Raspberry Pi 4
Remplace : MAIN_TEST_20260318.ino

Description :
    - Communique directement avec le VESC via UART (GPIO ou USB)
    - Lit les valeurs toutes les 50 ms (comme le loop() Arduino)
    - Envoie les commandes de contrôle reçues du dashboard
    - Diffuse les données en temps réel vers dashboard_app.py via une queue partagée
      (peut aussi tourner seul en mode CLI avec --no-gui)

Connexion VESC sur Raspberry Pi 4 :
    Option A — GPIO UART (recommandé, plus rapide) :
        VESC TX  →  Pi broche 10  (GPIO 15 / RXD)
        VESC RX  →  Pi broche 8   (GPIO 14 / TXD)
        GND      →  Pi broche 6   (GND)
        Port     :  /dev/ttyAMA0  (après dtoverlay=disable-bt dans /boot/config.txt)

    Option B — Adaptateur USB-UART (plus simple) :
        Port     :  /dev/ttyUSB0  ou  /dev/ttyACM0

Usage :
    python vesc_main.py                          # port auto-détecté
    python vesc_main.py --port /dev/ttyAMA0
    python vesc_main.py --port /dev/ttyUSB0 --baud 115200
"""

import argparse
import threading
import time
import queue
import sys
from enum import Enum, auto

import serial.tools.list_ports

from vesc_uart import VescUart

# ─────────────────────────────────────────────────────────────────────────────
# Mode de contrôle (même logique que l'Arduino)
# ─────────────────────────────────────────────────────────────────────────────
class ControlMode(Enum):
    NONE    = auto()
    RPM     = auto()
    CURRENT = auto()
    DUTY    = auto()
    BRAKE   = auto()

# ─────────────────────────────────────────────────────────────────────────────
# État global partagé entre les threads
# ─────────────────────────────────────────────────────────────────────────────
control_lock   = threading.Lock()
current_mode   = ControlMode.NONE
target_value   = 0.0

# Queue pour envoyer les données télémétriques vers le dashboard
telemetry_queue: queue.Queue = queue.Queue(maxsize=200)

# Queue pour recevoir les commandes du dashboard
command_queue:   queue.Queue = queue.Queue(maxsize=50)

# ─────────────────────────────────────────────────────────────────────────────
# Traitement des commandes (équivalent de processCommand() Arduino)
# ─────────────────────────────────────────────────────────────────────────────
def process_command(cmd: str):
    global current_mode, target_value
    cmd = cmd.strip()

    with control_lock:
        if cmd.startswith("Current="):
            val = float(cmd[8:])
            current_mode = ControlMode.CURRENT
            target_value = val
            _log(f"Nouveau courant: {val} A")

        elif cmd.startswith("BrakeCurrent="):
            val = float(cmd[13:])
            current_mode = ControlMode.BRAKE
            target_value = val
            _log(f"Nouveau courant de frein: {val} A")

        elif cmd.startswith("RPM="):
            val = float(cmd[4:])
            current_mode = ControlMode.RPM
            target_value = val
            _log(f"Nouveau RPM: {val}")

        elif cmd.startswith("Duty="):
            val = float(cmd[5:])
            current_mode = ControlMode.DUTY
            target_value = val
            _log(f"Nouveau Duty Cycle: {val}")

        elif cmd == "STOP":
            current_mode = ControlMode.NONE
            target_value = 0.0
            _log("Arrêt commande")


# ─────────────────────────────────────────────────────────────────────────────
# Envoi de la commande de contrôle au VESC (équivalent de maintainControl())
# ─────────────────────────────────────────────────────────────────────────────
def maintain_control(uart: VescUart):
    if uart is None:
    return

    with control_lock:
        mode = current_mode
        val  = target_value

    if mode == ControlMode.NONE:
        return
    try:
        if   mode == ControlMode.RPM:     uart.set_rpm(val)
        elif mode == ControlMode.CURRENT:  uart.set_current(val)
        elif mode == ControlMode.DUTY:     uart.set_duty(val)
        elif mode == ControlMode.BRAKE:    uart.set_brake_current(val)
    except Exception as e:
        _log(f"[Erreur commande VESC] {e}")


# ─────────────────────────────────────────────────────────────────────────────
# Lecture + calcul + émission des valeurs (équivalent de printVescValues())
# ─────────────────────────────────────────────────────────────────────────────
def read_and_emit(uart: VescUart):
    if not uart.get_values():
        _log("Echec de la lecture VESC")
        return

    d = uart.data
    out_voltage = d.inpVoltage * d.dutyCycleNow
    inp_power   = d.inpVoltage * d.avgInputCurrent
    out_power   = out_voltage  * d.avgMotorCurrent
    efficiency  = 0.0
    if d.wattHours > 1.0:
        efficiency = min(100.0, max(0.0, (d.wattHoursCharged / d.wattHours) * 100.0))

    now_ms = time.time() * 1000
    metrics = {
        "RPM":              d.rpm,
        "DutyCycle":        d.dutyCycleNow,
        "TachometerAbs":    d.tachometerAbs,
        "TempMotor":        d.tempMotor,
        "TempMosfet":       d.tempMosfet,
        "InpVoltage":       d.inpVoltage,
        "InpCurrent":       d.avgInputCurrent,
        "InpPower":         inp_power,
        "AmpHours":         d.ampHours,
        "WattHours":        d.wattHours,
        "OutVoltage":       out_voltage,
        "OutCurrent":       d.avgMotorCurrent,
        "OutPower":         out_power,
        "AmpHoursCharged":  d.ampHoursCharged,
        "WattHoursCharged": d.wattHoursCharged,
        "Efficiency":       efficiency,
    }

    for name, value in metrics.items():
        try:
            telemetry_queue.put_nowait({"name": name, "value": value, "time": now_ms})
        except queue.Full:
            pass  # le dashboard est trop lent, on perd des points — acceptable


# ─────────────────────────────────────────────────────────────────────────────
# Boucle principale VESC (équivalent du loop() Arduino — tourne dans un thread)
# ─────────────────────────────────────────────────────────────────────────────
def vesc_loop(uart: VescUart, stop_event: threading.Event):
    """
    Tourne à ~20 Hz (50 ms par itération).
    - Lit les commandes en attente
    - Maintient la commande de contrôle (50 Hz intégré via le même délai)
    - Lit et publie les valeurs VESC
    """
    import random
    last_control = 0.0

    while not stop_event.is_set():
        # --- Traiter les commandes reçues du dashboard ---
        try:
            while True:
                cmd = command_queue.get_nowait()
                process_command(cmd)
        except queue.Empty:
            pass

        # --- Maintenir le contrôle à ~50 Hz (toutes les 20 ms) ---
        now = time.time()
        if now - last_control >= 0.020:
            maintain_control(uart)
            last_control = now

        # --- Lire les valeurs VESC ou simuler ---
        if uart:
            read_and_emit(uart)
        else:
            # MODE SIMULATION
            now_ms = time.time() * 1000
            fake_data = {
                "RPM": random.randint(0, 3000),
                "DutyCycle": random.uniform(-1, 1),
                "TempMotor": random.uniform(20, 60),
                "TempMosfet": random.uniform(20, 70),
                "InpVoltage": 50.0,
                "InpCurrent": random.uniform(0, 30),
            }

            for name, value in fake_data.items():
                try:
                    telemetry_queue.put_nowait({
                        "name": name,
                        "value": value,
                        "time": now_ms
                    })
                except queue.Full:
                    pass

        time.sleep(0.050)   # ~20 Hz, comme le delay(50) de l'Arduino


# ─────────────────────────────────────────────────────────────────────────────
# Utilitaires
# ─────────────────────────────────────────────────────────────────────────────
def _log(msg: str):
    print(f"[VESC] {msg}", flush=True)
    try:
        telemetry_queue.put_nowait({"name": "__console__", "value": msg, "time": time.time() * 1000})
    except queue.Full:
        pass


def auto_detect_port() -> str:
    """Cherche un port série probable (ttyAMA0, ttyUSB*, ttyACM*)."""
    candidates = []
    for p in serial.tools.list_ports.comports():
        dev = p.device
        if any(k in dev for k in ("ttyAMA", "ttyUSB", "ttyACM", "ttyS")):
            candidates.append(dev)
    if candidates:
        _log(f"Ports détectés : {candidates}")
        return candidates[0]
    return "/dev/ttyAMA0"


# ─────────────────────────────────────────────────────────────────────────────
# Point d'entrée — peut être utilisé en standalone ou importé par dashboard_app.py
# ─────────────────────────────────────────────────────────────────────────────
def start(port: str = None, baudrate: int = 115200) -> tuple:
    if port is None:
        port = auto_detect_port()

    _log(f"Connexion VESC sur {port} @ {baudrate} baud ...")

    uart = None  # 🔥 IMPORTANT

    try:
        uart = VescUart(port, baudrate, timeout=0.1)
        uart.connect()
        _log("Connecté.")
    except Exception as e:
        _log(f"⚠️ Mode simulation (pas de VESC): {e}")
        uart = None

    stop_event = threading.Event()
    t = threading.Thread(
        target=vesc_loop,
        args=(uart, stop_event),
        daemon=True,
        name="vesc_loop"
    )
    t.start()

    return uart, stop_event, t


def stop(uart: VescUart, stop_event: threading.Event, thread: threading.Thread):
    """Arrêt propre du thread VESC."""
    stop_event.set()
    thread.join(timeout=2)
    uart.disconnect()
    _log("Déconnecté.")


# ─────────────────────────────────────────────────────────────────────────────
# Mode CLI standalone (sans dashboard graphique)
# ─────────────────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="VESC Main — Raspberry Pi 4")
    parser.add_argument("--no-vesc", action="store_true", help="Mode simulation sans VESC")
    parser.add_argument("--port",  default=None,   help="Port série (ex: /dev/ttyAMA0)")
    parser.add_argument("--baud",  default=115200,  type=int)
    parser.add_argument("--no-gui", action="store_true", help="Afficher les données en console sans dashboard")
    args = parser.parse_args()

    uart, stop_ev, thread = start(args.port, args.baud)

    if args.no_gui:
        print("Mode CLI — Ctrl+C pour arrêter")
        print("Commandes disponibles : Current=<A>, BrakeCurrent=<A>, RPM=<val>, Duty=<-1..1>, STOP")
        try:
            while True:
                # Afficher les données télémétriques
                try:
                    item = telemetry_queue.get(timeout=0.2)
                    if item["name"] == "__console__":
                        print(f"  {item['value']}")
                    else:
                        print(f"  {item['name']:24s} = {item['value']:.4f}")
                except queue.Empty:
                    pass

                # Lire les commandes clavier
                import select
                if select.select([sys.stdin], [], [], 0)[0]:
                    line = sys.stdin.readline().strip()
                    if line:
                        command_queue.put(line)

        except KeyboardInterrupt:
            print("\nArrêt...")
    else:
        # Démarrer le dashboard graphique
        from dashboard_app import run_dashboard
        run_dashboard(uart, stop_ev, thread)
