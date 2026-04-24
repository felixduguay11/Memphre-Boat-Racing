from flask import Flask, render_template
from flask_socketio import SocketIO
import serial
import json
import threading

app = Flask(__name__)
socketio = SocketIO(app)

ser = serial.Serial("/dev/serial0",115200,timeout=1)

@app.route("/")
def index():
    return render_template("index.html")

def serial_reader():

    while True:

        line = ser.readline().decode(errors="ignore").strip()

        if not line:
            continue

        try:
            data=json.loads(line)

            socketio.emit("vesc_data",data)

        except:
            pass

threading.Thread(target=serial_reader,daemon=True).start()

@socketio.on("command")
def handle_command(cmd):
    ser.write((cmd+"\n").encode())

if __name__ == "__main__":
    socketio.run(app,host="0.0.0.0",port=5000)