#!/usr/bin/env python3
"""
dashboard_app.py — Dashboard VESC plein écran sur l'écran HDMI du Raspberry Pi 4
Remplace : server.py  +  accès navigateur web distant

Description :
    - Démarre un micro-serveur Flask+SocketIO local (127.0.0.1:5000) identique
      à l'ancien server.py MAIS récupère les données depuis vesc_main.py au lieu
      du port série.
    - Ouvre le dashboard.html dans une fenêtre PyQt5 WebEngine plein écran sur
      l'écran HDMI — aucun navigateur séparé n'est nécessaire.

Dépendances :
    pip install flask flask-socketio pyserial PyQt5 PyQtWebEngine

Lancement :
    python dashboard_app.py                         # connexion VESC auto
    python dashboard_app.py --port /dev/ttyAMA0
    python vesc_main.py                             # équivalent (importe dashboard_app)

Astuce Raspberry Pi — lancer automatiquement au démarrage :
    Ajoutez dans /etc/rc.local (avant exit 0) :
        export DISPLAY=:0
        su pi -c "python /home/pi/PALARDY/raspberry_pi/vesc_main.py" &
"""

import sys
import os
import argparse
import threading
import time
import queue

from flask import Flask
from flask_socketio import SocketIO

# Import du module VESC (doit être dans le même répertoire)
import vesc_main as vm

# ─────────────────────────────────────────────────────────────────────────────
# Serveur Flask + SocketIO (couche transport vers le dashboard HTML)
# ─────────────────────────────────────────────────────────────────────────────
flask_app = Flask(__name__)
flask_app.config['SECRET_KEY'] = 'vesc_dashboard_secret'
socketio  = SocketIO(flask_app, cors_allowed_origins="*", async_mode='threading')

# Chemin vers dashboard.html (même dossier que ce script)
DASH_DIR = os.path.dirname(os.path.abspath(__file__))


@flask_app.route('/')
def index():
    path = os.path.join(DASH_DIR, 'dashboard.html')
    with open(path, 'r', encoding='utf-8') as f:
        # Injecter un flag JS pour signaler au dashboard qu'il est en mode "embarqué"
        # (cache les boutons de connexion port-série qui ne servent plus à rien)
        html = f.read()
        html = html.replace(
            '<script>',
            '<script>\nwindow.EMBEDDED_MODE = true;\n',
            1
        )
        return html


@socketio.on('connect')
def on_connect():
    # En mode embarqué, la connexion série est déjà établie dans vesc_main.py
    socketio.emit('connection_status', {'connected': True, 'port': 'VESC UART (Pi)'})
    socketio.emit('console', {'msg': '✅ Raspberry Pi connecté au VESC'})


@socketio.on('send_command')
def on_send_command(data):
    cmd = data.get('cmd', '').strip()
    if cmd:
        try:
            vm.command_queue.put_nowait(cmd)
            socketio.emit('console', {'msg': f'→ {cmd}'})
        except queue.Full:
            socketio.emit('console', {'msg': '⚠️ File de commandes pleine'})


@socketio.on('list_ports')
def on_list_ports():
    # En mode embarqué, on expose seulement le port actif
    socketio.emit('port_list', {'ports': ['VESC UART (Pi)']})


# ─────────────────────────────────────────────────────────────────────────────
# Thread de transfert télémétrie → SocketIO
# ─────────────────────────────────────────────────────────────────────────────
def _telemetry_bridge(stop_event: threading.Event):
    """
    Lit la queue de télémétriedepuis vesc_main et la pousse vers le dashboard
    via SocketIO (même protocole que l'ancien server.py).
    """
    while not stop_event.is_set():
        try:
            item = vm.telemetry_queue.get(timeout=0.05)
            if item["name"] == "__console__":
                socketio.emit('console', {'msg': item["value"]})
            else:
                socketio.emit('telemetry', {
                    'name':  item['name'],
                    'value': item['value'],
                    'time':  item['time']
                })
        except queue.Empty:
            pass
        except Exception:
            pass


# ─────────────────────────────────────────────────────────────────────────────
# Fenêtre PyQt5 WebEngine (plein écran HDMI)
# ─────────────────────────────────────────────────────────────────────────────
def _start_webview():
    """Ouvre le dashboard dans une fenêtre plein écran PyQt5 WebEngine."""
    try:
        from PyQt5.QtWidgets import QApplication
        from PyQt5.QtWebEngineWidgets import QWebEngineView
        from PyQt5.QtCore import QUrl, Qt
        from PyQt5.QtGui import QKeySequence
        from PyQt5.QtWidgets import QShortcut
    except ImportError:
        print("[Dashboard] PyQt5 / PyQtWebEngine non installé.")
        print("  →  pip install PyQt5 PyQtWebEngine")
        print("  →  Le dashboard reste accessible via http://127.0.0.1:5000")
        return

    # Attendre que Flask soit prêt
    time.sleep(1.2)

    app = QApplication(sys.argv)

    view = QWebEngineView()
    view.setWindowTitle("VESC Dashboard")
    view.load(QUrl("http://127.0.0.1:5000"))

    # Plein écran (écran HDMI du Pi)
    view.showFullScreen()

    # Raccourci clavier : Echap pour quitter le plein écran, F pour le rétablir
    def toggle_fullscreen():
        if view.isFullScreen():
            view.showNormal()
        else:
            view.showFullScreen()

    esc = QShortcut(QKeySequence(Qt.Key_Escape), view)
    esc.activated.connect(toggle_fullscreen)

    f_key = QShortcut(QKeySequence("F"), view)
    f_key.activated.connect(toggle_fullscreen)

    q_key = QShortcut(QKeySequence("Ctrl+Q"), view)
    q_key.activated.connect(app.quit)

    app.exec_()


# ─────────────────────────────────────────────────────────────────────────────
# Point d'entrée principal
# ─────────────────────────────────────────────────────────────────────────────
def run_dashboard(uart=None, vesc_stop_event=None, vesc_thread=None):
    """
    Démarre tout le système :
      1. vesc_main (UART VESC) si pas déjà démarré
      2. Pont télémétrie → SocketIO
      3. Serveur Flask en arrière-plan
      4. Fenêtre PyQt5 plein écran
    """
    # 1 — Démarrer vesc_main si nécessaire
    if uart is None:
        uart, vesc_stop_event, vesc_thread = vm.start()

    bridge_stop = threading.Event()

    # 2 — Pont télémétrie
    bridge_thread = threading.Thread(
        target=_telemetry_bridge,
        args=(bridge_stop,),
        daemon=True,
        name="telem_bridge"
    )
    bridge_thread.start()

    # 3 — Serveur Flask en arrière-plan
    flask_thread = threading.Thread(
        target=lambda: socketio.run(
            flask_app,
            host='127.0.0.1',
            port=5000,
            debug=False,
            allow_unsafe_werkzeug=True,
            use_reloader=False
        ),
        daemon=True,
        name="flask_server"
    )
    flask_thread.start()
    print("🚀 Serveur dashboard → http://127.0.0.1:5000")

    # 4 — Fenêtre PyQt5 plein écran (bloquant jusqu'à fermeture)
    _start_webview()

    # 5 — Nettoyage
    bridge_stop.set()
    if vesc_stop_event:
        vm.stop(uart, vesc_stop_event, vesc_thread)


# ─────────────────────────────────────────────────────────────────────────────
# Lancement direct
# ─────────────────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="VESC Dashboard — Raspberry Pi 4 HDMI")
    parser.add_argument("--port",  default=None,   help="Port série VESC (ex: /dev/ttyAMA0)")
    parser.add_argument("--baud",  default=115200,  type=int)
    parser.add_argument("--no-fullscreen", action="store_true",
                        help="Fenêtre normale au lieu du plein écran")
    args = parser.parse_args()

    run_dashboard()
