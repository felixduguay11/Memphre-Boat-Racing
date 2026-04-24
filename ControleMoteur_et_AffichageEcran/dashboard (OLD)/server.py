#!/usr/bin/env python3
"""
VESC Serial Bridge Server
Reads serial data from Arduino, serves a web dashboard with real-time graphs and controls.

Usage:
    pip install pyserial flask flask-socketio
    python server.py --port COM4 --baud 115200
"""

import argparse
import threading
import time
import re
from flask import Flask, render_template_string, send_from_directory
from flask_socketio import SocketIO
import serial
import serial.tools.list_ports
import os

app = Flask(__name__)
app.config['SECRET_KEY'] = 'vesc_dashboard_secret'
socketio = SocketIO(app, cors_allowed_origins="*", async_mode='threading')

# ── State ──────────────────────────────────────────────────────────────────────
serial_conn = None
serial_lock = threading.Lock()
running = False
serial_thread = None

# ── Serial reading thread ──────────────────────────────────────────────────────
def read_serial():
    global running, serial_conn
    buffer = ""
    while running:
        try:
            with serial_lock:
                if serial_conn and serial_conn.in_waiting:
                    raw = serial_conn.read(serial_conn.in_waiting).decode('utf-8', errors='replace')
                    buffer += raw

            while '\n' in buffer:
                line, buffer = buffer.split('\n', 1)
                line = line.strip()
                if not line:
                    continue

                if line.startswith('>'):
                    # Telemetry: >VarName:value
                    match = re.match(r'^>([^:]+):(.+)$', line)
                    if match:
                        name = match.group(1).strip()
                        try:
                            value = float(match.group(2).strip())
                            socketio.emit('telemetry', {'name': name, 'value': value, 'time': time.time() * 1000})
                        except ValueError:
                            pass
                else:
                    # Console message
                    socketio.emit('console', {'msg': line})

        except Exception as e:
            socketio.emit('console', {'msg': f'[Serial error] {e}'})
            time.sleep(0.1)

        time.sleep(0.01)

# ── SocketIO events ───────────────────────────────────────────────────────────
@socketio.on('connect')
def on_connect():
    ports = [p.device for p in serial.tools.list_ports.comports()]
    socketio.emit('port_list', {'ports': ports})

@socketio.on('open_port')
def on_open_port(data):
    global serial_conn, running, serial_thread
    port = data.get('port')
    baud = int(data.get('baud', 115200))
    try:
        with serial_lock:
            if serial_conn and serial_conn.is_open:
                serial_conn.close()
            serial_conn = serial.Serial(port, baud, timeout=0.1)
        running = True
        serial_thread = threading.Thread(target=read_serial, daemon=True)
        serial_thread.start()
        socketio.emit('console', {'msg': f'✅ Connecté à {port} @ {baud} baud'})
        socketio.emit('connection_status', {'connected': True, 'port': port})
    except Exception as e:
        socketio.emit('console', {'msg': f'❌ Erreur: {e}'})
        socketio.emit('connection_status', {'connected': False})

@socketio.on('close_port')
def on_close_port():
    global running, serial_conn
    running = False
    time.sleep(0.1)
    with serial_lock:
        if serial_conn and serial_conn.is_open:
            serial_conn.close()
    socketio.emit('console', {'msg': '🔌 Déconnecté'})
    socketio.emit('connection_status', {'connected': False})

@socketio.on('send_command')
def on_send_command(data):
    cmd = data.get('cmd', '')
    with serial_lock:
        if serial_conn and serial_conn.is_open:
            serial_conn.write((cmd + '\n').encode('utf-8'))
            socketio.emit('console', {'msg': f'→ {cmd}'})
        else:
            socketio.emit('console', {'msg': '⚠️ Port série non connecté'})

@socketio.on('list_ports')
def on_list_ports():
    ports = [p.device for p in serial.tools.list_ports.comports()]
    socketio.emit('port_list', {'ports': ports})

# ── HTML Dashboard ─────────────────────────────────────────────────────────────
@app.route('/')
def index():
    with open(os.path.join(os.path.dirname(__file__), 'dashboard.html'), 'r', encoding='utf-8') as f:
        return f.read()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='VESC Dashboard Server')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', default=5000, type=int)
    args = parser.parse_args()
    print(f"\n🚀 VESC Dashboard → http://{args.host}:{args.port}\n")
    socketio.run(app, host=args.host, port=args.port, debug=False, allow_unsafe_werkzeug=True)
