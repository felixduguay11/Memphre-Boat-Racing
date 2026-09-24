"""
Hydrofoil Dashboard — GUI Python (tkinter)  v3
Lancer avec : python3 gui.py
Dépendances : pip3 install pyserial   (tkinter est inclus avec Python)

═══════════════════════════════════════════════════════════════
CLÉS JSON ENVOYÉES PAR LE TEENSY (un objet par ESC, 1 s/update)
═══════════════════════════════════════════════════════════════
  {"id":10, "vin":48.2, "iin":5.2, "pin":250.0, "ahin":0.12,
   "whin":5.23, "vout":38.1, "iout":42.1, "pout":1620.0,
   "ahout":0.09, "whout":4.1, "effwh":0.79, "eff":0.83,
   "rpm":3200, "erpm":9600, "duty":0.79, "tach":12345,
   "tempmot":26.2, "tempmos":26.3}

═══════════════════════════════════════════════════════════════
DONNÉES MANQUANTES — À AJOUTER DANS LE TEENSY
═══════════════════════════════════════════════════════════════
  Ces valeurs ne viennent pas des ESCs. Il faut les envoyer
  dans un JSON séparé depuis le Teensy, par exemple dans loop() :

  {"type":"sensors",
   "speed_kmh": 18.1,      ← GPS (ex: BN-880) ou calcul RPM+pas hélice
   "roll":  26.51,          ← IMU (ex: MPU-6050, BNO055)
   "pitch": 54.19,
   "yaw":  -26.22,
   "sonar_avant": 54.31,   ← HC-SR04 ou JSN-SR04T (cm)
   "sonar_arga":  54.19,
   "sonar_ardr":  53.87,
   "servo_avant":  0.62,   ← lecture angle servo (°)
   "servo_arga":  -2.98,
   "servo_ardr":   5.71,
   "state": "RUN"}          ← "RUN", "IDLE", "FAULT"

  Exemple Teensy (à appeler dans loop() toutes les ~100 ms) :
  ─────────────────────────────────────────────────────────────
  void sendSensors() {
    PI_SERIAL.print("{\"type\":\"sensors\"");
    PI_SERIAL.print(",\"speed_kmh\":"); PI_SERIAL.print(gpsSpeed, 1);
    PI_SERIAL.print(",\"roll\":");      PI_SERIAL.print(imu.roll, 2);
    PI_SERIAL.print(",\"pitch\":");     PI_SERIAL.print(imu.pitch, 2);
    PI_SERIAL.print(",\"yaw\":");       PI_SERIAL.print(imu.yaw, 2);
    PI_SERIAL.print(",\"sonar_avant\":"); PI_SERIAL.print(sonar1_cm, 2);
    PI_SERIAL.print(",\"sonar_arga\":"); PI_SERIAL.print(sonar2_cm, 2);
    PI_SERIAL.print(",\"sonar_ardr\":"); PI_SERIAL.print(sonar3_cm, 2);
    PI_SERIAL.print(",\"servo_avant\":"); PI_SERIAL.print(servo1_deg, 2);
    PI_SERIAL.print(",\"servo_arga\":"); PI_SERIAL.print(servo2_deg, 2);
    PI_SERIAL.print(",\"servo_ardr\":"); PI_SERIAL.print(servo3_deg, 2);
    PI_SERIAL.print(",\"state\":\"RUN\"");
    PI_SERIAL.println("}");
  }

═══════════════════════════════════════════════════════════════
CONFIG BATTERIE — adapter à votre pack
  BAT_CELLS    : cellules LiPo en série
  BAT_CAPACITY : capacité totale en Ah
═══════════════════════════════════════════════════════════════
"""

from ast import main
import tkinter as tk
import serial
import json
import threading
import time
import queue

# ─── CONFIG ────────────────────────────────────────────────────────────────────
SERIAL_PORT  = "/dev/serial0"
BAUD         = 115200
WATCHDOG_SEC = 1.5
DEMO_MODE    = False # True = données simulées sans port série

BAT_CELLS    = 12      # cellules LiPo en série (12S → ~50.4 V plein)
BAT_CAPACITY = 44.0    # Ah totaux du pack

CELL_V_FULL  = 4.20
CELL_V_EMPTY = 3.50
V_FULL       = BAT_CELLS * CELL_V_FULL    # 50.4 V pour 12S
V_EMPTY      = BAT_CELLS * CELL_V_EMPTY   # 42.0 V pour 12S

# ─── COULEURS ──────────────────────────────────────────────────────────────────
BG       = "#b8cfe0"
PANEL_BG = "#b8cfe0"
CARD_BG  = "#ddeef7"
BTN_BG   = "#cce0ef"
BTN_ACT  = "#8ab8d4"
BOX_BG   = "white"
HDR_BG   = "#cce0ef"
GREEN    = "#22bb44"
RED      = "#dd3333"
ORANGE   = "#dd7700"


# ═══════════════════════════════════════════════════════════════════════════════
class HydrofoilDashboard:

    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.geometry("1024x600")
        self.root.tk.call('tk', 'scaling', 1.6)
        self.root.title("Hydrofoil Dashboard")
        self.root.configure(bg=BG)
        self.root.attributes("-fullscreen", True)
        #self.root.state("zoomed")
        self.root.bind("<Escape>", lambda e: self.root.attributes("-fullscreen", False))
        self.root.bind("<F11>",    lambda e: self.root.attributes("-fullscreen", True))

        self.queue:     queue.Queue = queue.Queue()
        self.connected: bool        = False
        self.last_msg:  float       = time.time()

        # Cache ESC pour calculs dérivés
        self._esc: dict = {10: {}, 11: {}}

        # Timer
        self.timer_running = False
        self.timer_start   = 0.0
        self.timer_elapsed = 0.0

        # Mode/Intensité
        self.current_mode      = tk.StringVar(value="Asservissement")
        self.current_intensity = tk.StringVar(value="Moyen")

        self._build_ui()
        self._start_serial()
        self._tick()

    # ═══════════════════════════════════════════════════════════════════════════
    # Construction UI
    # ═══════════════════════════════════════════════════════════════════════════

    def _build_ui(self):
        # Conteneur des pages empilées
        container = tk.Frame(self.root, bg=BG)
        container.pack(fill="both", expand=True)
        container.grid_rowconfigure(0, weight=1)
        container.grid_columnconfigure(0, weight=1)

        self.pages: dict = {}
        builders = {
            "Setup":     self._build_setup,
            "Principal": self._build_principal,
            "Contrôle":  self._build_controle,
            "Moteur":    self._build_moteur,
        }
        for name, builder in builders.items():
            frame = tk.Frame(container, bg=PANEL_BG)
            frame.grid(row=0, column=0, sticky="nsew")
            builder(frame)
            self.pages[name] = frame

        self.show_page("Principal")

    def _page_nav(self, parent):

        nav = tk.Frame(parent, bg=PANEL_BG)

        pages = ["Setup", "Principal", "Contrôle", "Moteur"]
        
        self._nav_btn_refs = {}

        for page in pages:
            b = tk.Button(
                nav,
                text=page,
                bg=BTN_BG,
                font=("Arial", 11, "bold"),
                width=10,
                relief="raised",
                command=lambda p=page: self.show_page(p)
            )
            b.pack(pady=4)

        return nav

    def show_page(self, name: str):
        self.pages[name].tkraise()
        for pname, btn in self._nav_btn_refs.items():
            btn.config(bg=BTN_ACT if pname == name else BTN_BG)

    # ── Helpers ────────────────────────────────────────────────────────────────

    def _clock_label(self, parent) -> tk.Label:
        lbl = tk.Label(parent, text="--:--:--", bg=PANEL_BG, 
                       font=("Arial", 20, "bold"), fg="#222")
        lbl.place(relx=1.0, rely=0.0, anchor="ne", x=-8, y=6)
        return lbl

    def _card_frame(self, parent, title: str) -> tk.Frame:
        outer = tk.Frame(parent, bg=CARD_BG, bd=1, relief="solid")
        outer.pack(fill="x", pady=(0, 6))
        tk.Label(outer, text=title, bg=HDR_BG, font=("Arial", 10, "bold"),
                 anchor="center").pack(fill="x")
        inner = tk.Frame(outer, bg=CARD_BG)
        inner.pack(padx=5, pady=(2, 5))
        return inner

    def _table_row(self, parent, label: str, row: int,
                   lbl_w=7, val_w=9) -> tk.Label:
        tk.Label(parent, text=label, bg=HDR_BG, font=("Arial", 10, "bold"),
                 relief="solid", bd=1, width=lbl_w, anchor="w", padx=3
                 ).grid(row=row, column=0, sticky="nsew")
        v = tk.Label(parent, text="--", bg=BOX_BG, font=("Arial", 10, "bold"),
                     relief="solid", bd=1, width=val_w, anchor="e", padx=3)
        v.grid(row=row, column=1, sticky="nsew")
        return v

    def _temp_row(self, parent, label: str, row: int) -> tk.Label:
        tk.Label(parent, text=label, bg=CARD_BG, font=("Arial", 10),
                 width=10, anchor="w").grid(row=row, column=0, sticky="w")
        v = tk.Label(parent, text="--°C", bg=BOX_BG, font=("Arial", 10, "bold"),
                     bd=1, relief="solid", width=7, fg=ORANGE)
        v.grid(row=row, column=1, padx=(4, 0), pady=1)
        return v

    # ═══════════════════════════════════════════════════════════════════════════
    # PANNEAU 1 — SETUP
    # ═══════════════════════════════════════════════════════════════════════════

    def _build_setup(self, parent):

        nav = self._page_nav(parent)
        nav.place(relx=0.97, rely=0.5, anchor="e")
        self.clk_setup = self._clock_label(parent)

        body = tk.Frame(parent, bg=PANEL_BG)
        body.pack(fill="both", expand=True, padx=30, pady=30)

        # PID + Intensité
        pid_row = tk.Frame(body, bg=PANEL_BG)
        pid_row.pack(anchor="w")

        int_fr = tk.Frame(pid_row, bg=PANEL_BG)
        int_fr.pack(side="left", padx=(0, 12), anchor="n", pady=4)
        self._intensity_btns: dict = {}
        for label in ["Faible", "Moyen", "Fort"]:
            b = tk.Button(int_fr, text=label, bg=BTN_BG, relief="groove",
                          font=("Arial", 10), width=8,
                          command=lambda l=label: self._set_intensity(l))
            b.pack(pady=3)
            self._intensity_btns[label] = b

        pid_fr = tk.Frame(pid_row, bg=PANEL_BG)
        pid_fr.pack(side="left")
        self._pid_entries: dict = {}
        for i, (letter, key, default) in enumerate([
                ("P", "p", "3.04"), ("I", "i", "5.98"), ("D", "d", "0.72")]):
            tk.Label(pid_fr, text=letter, bg=PANEL_BG,
                     font=("Arial", 26, "bold"), width=2).grid(row=i, column=0)
            e = tk.Entry(pid_fr, font=("Arial", 16, "bold"), width=8,
                         justify="center", relief="solid", bd=1)
            e.insert(0, default)
            e.grid(row=i, column=1, padx=4, pady=3)
            self._pid_entries[key] = e

        tk.Button(body, text="Envoyer PID", bg=BTN_BG, font=("Arial", 10),
                  command=self._send_pid).pack(anchor="w", pady=(4, 8))

        tk.Frame(body, bg="#9ab5c8", height=1).pack(fill="x", pady=(0, 8))

        # MODE
        mode_row = tk.Frame(body, bg=PANEL_BG)
        mode_row.pack(anchor="w")
        tk.Label(mode_row, text="MODE", bg=PANEL_BG,
                 font=("Arial", 22, "bold")).pack(side="left", padx=(0, 14))
        mode_fr = tk.Frame(mode_row, bg=PANEL_BG)
        mode_fr.pack(side="left")
        self._mode_btns: dict = {}
        for m in ["Asservissement", "Hydrofoils Fixes", "Pas d'Hydrofoils"]:
            b = tk.Button(mode_fr, text=m, bg=BTN_BG, relief="groove",
                          font=("Arial", 11), width=16,
                          command=lambda mv=m: self._set_mode(mv))
            b.pack(pady=3, anchor="w")
            self._mode_btns[m] = b

        self._refresh_intensity()
        self._refresh_mode()

    def _set_intensity(self, label):
        self.current_intensity.set(label)
        self._refresh_intensity()
        self._send_command(f"intensity:{label.lower()}")

    def _refresh_intensity(self):
        cur = self.current_intensity.get()
        for lbl, btn in self._intensity_btns.items():
            btn.config(bg=BTN_ACT if lbl == cur else BTN_BG)

    def _set_mode(self, mode):
        self.current_mode.set(mode)
        self._refresh_mode()
        self._send_command(f"mode:{mode}")

    def _refresh_mode(self):
        cur = self.current_mode.get()
        for m, btn in self._mode_btns.items():
            btn.config(bg=BTN_ACT if m == cur else BTN_BG)

    def _send_pid(self):
        p = self._pid_entries["p"].get()
        i = self._pid_entries["i"].get()
        d = self._pid_entries["d"].get()
        self._send_command(f"PID={p},{i},{d}")

    # ═══════════════════════════════════════════════════════════════════════════
    # PANNEAU 2 — PRINCIPAL
    # ═══════════════════════════════════════════════════════════════════════════

    def _build_principal(self, parent):
        nav = self._page_nav(parent)
        nav.place(relx=0.97, rely=0.5, anchor="e")
        self.clk_principal = self._clock_label(parent)
        
        main = tk.Frame(parent, bg=BG)
        main.pack(fill="both", expand=True)

        # Timer coin haut droit
        timer_fr = tk.Frame(parent, bg=PANEL_BG)
        timer_fr.place(relx=1.0, rely=0.0, anchor="ne", x=-8, y=24)
        tk.Label(timer_fr, text="TIMER", bg=PANEL_BG,
                 font=("Arial", 9, "bold")).pack()
        self.lbl_timer = tk.Label(timer_fr, text="00:00:00", bg=PANEL_BG,
                                  font=("Arial", 12, "bold"))
        self.lbl_timer.pack()
        self.btn_timer_onoff = tk.Button(timer_fr, text="ON/OFF", bg=GREEN,
                                          fg="white", font=("Arial", 9, "bold"),
                                          width=7, command=self._toggle_timer)
        self.btn_timer_onoff.pack(pady=2)
        tk.Button(timer_fr, text="RESET", bg=BTN_BG, font=("Arial", 9),
                  width=7, command=self._reset_timer).pack()

        
        # Corps
        body = tk.Frame(parent, bg=PANEL_BG)
        body.pack(fill="both", expand=True, padx=20, pady=20)

        # Colonne gauche : batterie + températures
        left = tk.Frame(body, bg=PANEL_BG)
        left.pack(side="left", anchor="nw", padx=(0, 16))

        bat_inner = self._card_frame(left, "BATTERIE")
        self.lbl_bat_charge = tk.Label(bat_inner, text="Charge : --%",
                                        bg=CARD_BG, font=("Arial", 10, "bold"))
        self.lbl_bat_charge.pack(anchor="w")
        self.lbl_bat_kwh    = tk.Label(bat_inner, text="-- kWh",
                                        bg=CARD_BG, font=("Arial", 10, "bold"))
        self.lbl_bat_kwh.pack(anchor="w")
        self.lbl_bat_output = tk.Label(bat_inner, text="Output : -- kW",
                                        bg=CARD_BG, font=("Arial", 10))
        self.lbl_bat_output.pack(anchor="w")
        self.lbl_bat_remain = tk.Label(bat_inner, text="-- restant",
                                        bg=CARD_BG, font=("Arial", 10))
        self.lbl_bat_remain.pack(anchor="w")
        self.bat_canvas = tk.Canvas(bat_inner, width=112, height=30,
                                    bg=CARD_BG, highlightthickness=0)
        self.bat_canvas.pack(pady=4)
        self._bat_bars = [
            self.bat_canvas.create_rectangle(4 + i*22, 4, 4 + i*22 + 18, 26,
                                              fill=GREEN, outline="#333")
            for i in range(5)
        ]

        temp_inner = self._card_frame(left, "TEMPÉRATURES")
        self.lbl_temp_bat = self._temp_row(temp_inner, "Batterie",  0)
        self.lbl_temp_m1  = self._temp_row(temp_inner, "MOSFET 1", 1)
        self.lbl_temp_m2  = self._temp_row(temp_inner, "MOSFET 2", 2)

        # Colonne droite : vitesse + stats + état
        right = tk.Frame(body, bg=PANEL_BG)
        right.pack(side="left", anchor="nw", pady=(8, 0))

        self.lbl_speed = tk.Label(right, text="--,- km/h", bg=PANEL_BG,
                                   font=("Arial", 44, "bold"), anchor="w")
        self.lbl_speed.pack(anchor="w")
        tk.Frame(right, bg="#444", height=2).pack(fill="x", pady=6)
        self.lbl_pout = tk.Label(right, text="Pout : -- kW",
                                  bg=PANEL_BG, font=("Arial", 14, "bold"))
        self.lbl_pout.pack(anchor="w")
        self.lbl_eff  = tk.Label(right, text="Efficacité : --%",
                                  bg=PANEL_BG, font=("Arial", 14, "bold"))
        self.lbl_eff.pack(anchor="w")
        self.lbl_state_p = tk.Label(right, text="STATE :   --",
                                     bg=BOX_BG, font=("Arial", 15, "bold"),
                                     bd=2, relief="solid", padx=10, pady=4)
        self.lbl_state_p.pack(anchor="w", pady=(18, 0))

    # ═══════════════════════════════════════════════════════════════════════════
    # PANNEAU 3 — CONTRÔLE
    # ═══════════════════════════════════════════════════════════════════════════

    def _build_controle(self, parent):
        nav = self._page_nav(parent)
        nav.place(relx=0.97, rely=0.5, anchor="e")
        self.clk_controle = self._clock_label(parent)

        body = tk.Frame(parent, bg=PANEL_BG)
        body.pack(fill="both", expand=True, padx=20, pady=20)

        # IMU
        imu_fr = tk.Frame(body, bg=PANEL_BG)
        imu_fr.grid(row=0, column=0, padx=(0, 16), sticky="nw")
        tk.Label(imu_fr, text="IMU", bg=HDR_BG, font=("Arial", 10, "bold"),
                 bd=1, relief="solid").grid(row=0, column=0, columnspan=2, sticky="ew")
        self.lbl_roll  = self._table_row(imu_fr, "ROLL",  1)
        self.lbl_pitch = self._table_row(imu_fr, "PITCH", 2)
        self.lbl_yaw   = self._table_row(imu_fr, "YAW",   3)

        # Sonars
        son_fr = tk.Frame(body, bg=PANEL_BG)
        son_fr.grid(row=0, column=1, sticky="nw")
        tk.Label(son_fr, text="SONARS", bg=HDR_BG, font=("Arial", 10, "bold"),
                 bd=1, relief="solid").grid(row=0, column=0, columnspan=2, sticky="ew")
        self.lbl_son_avant = self._table_row(son_fr, "AVANT", 1, val_w=10)
        self.lbl_son_arga  = self._table_row(son_fr, "ARGa",  2, val_w=10)
        self.lbl_son_ardr  = self._table_row(son_fr, "ARDr",  3, val_w=10)

        # Servos
        srv_fr = tk.Frame(body, bg=PANEL_BG)
        srv_fr.grid(row=1, column=1, pady=(10, 0), sticky="nw")
        tk.Label(srv_fr, text="SERVOS", bg=HDR_BG, font=("Arial", 10, "bold"),
                 bd=1, relief="solid").grid(row=0, column=0, columnspan=2, sticky="ew")
        self.lbl_srv_avant = self._table_row(srv_fr, "AVANT", 1, val_w=10)
        self.lbl_srv_arga  = self._table_row(srv_fr, "ARGa",  2, val_w=10)
        self.lbl_srv_ardr  = self._table_row(srv_fr, "ARDr",  3, val_w=10)

        # STATE
        self.lbl_state_c = tk.Label(parent, text="STATE :   --",
                                     bg=BOX_BG, font=("Arial", 14, "bold"),
                                     bd=2, relief="solid", padx=10, pady=4)
        self.lbl_state_c.place(relx=0.0, rely=1.0, anchor="sw", x=12, y=-12)

    # ═══════════════════════════════════════════════════════════════════════════
    # PANNEAU 4 — MOTEUR
    # ═══════════════════════════════════════════════════════════════════════════

    def _build_moteur(self, parent):
        nav = self._page_nav(parent)
        nav.place(relx=0.97, rely=0.5, anchor="e")
        self.clk_moteur = self._clock_label(parent)

        body = tk.Frame(parent, bg=PANEL_BG)
        body.pack(fill="both", expand=True, padx=20, pady=20)

        # Colonne gauche : batterie + températures
        left = tk.Frame(body, bg=PANEL_BG)
        left.grid(row=0, column=0, padx=(0, 16), sticky="nw")

        bat2 = self._card_frame(left, "BATTERIE")
        self.lbl_m_bat_charge = tk.Label(bat2, text="Charge : --%",
                                          bg=CARD_BG, font=("Arial", 10, "bold"))
        self.lbl_m_bat_charge.pack(anchor="w")
        self.lbl_m_bat_kwh    = tk.Label(bat2, text="-- kWh",
                                          bg=CARD_BG, font=("Arial", 10, "bold"))
        self.lbl_m_bat_kwh.pack(anchor="w")
        self.lbl_m_bat_output = tk.Label(bat2, text="Output : -- kW",
                                          bg=CARD_BG, font=("Arial", 10))
        self.lbl_m_bat_output.pack(anchor="w")
        self.lbl_m_bat_remain = tk.Label(bat2, text="-- restant",
                                          bg=CARD_BG, font=("Arial", 10))
        self.lbl_m_bat_remain.pack(anchor="w")
        self.bat_canvas2 = tk.Canvas(bat2, width=112, height=30,
                                     bg=CARD_BG, highlightthickness=0)
        self.bat_canvas2.pack(pady=4)
        self._bat_bars2 = [
            self.bat_canvas2.create_rectangle(4 + i*22, 4, 4 + i*22 + 18, 26,
                                               fill=GREEN, outline="#333")
            for i in range(5)
        ]

        temp2 = self._card_frame(left, "TEMPÉRATURES")
        self.lbl_m_m1   = self._temp_row(temp2, "MOSFET 1", 0)
        self.lbl_m_mot1 = self._temp_row(temp2, "MOTOR 1",  1)
        self.lbl_m_m2   = self._temp_row(temp2, "MOSFET 2", 2)
        self.lbl_m_mot2 = self._temp_row(temp2, "MOTOR 2",  3)
        self.lbl_m_bat  = self._temp_row(temp2, "Batterie",  4)

        # Colonne droite : tables ESC
        right = tk.Frame(body, bg=PANEL_BG)
        right.grid(row=0, column=1, sticky="nw")
        self.esc1_vals = self._esc_table(right, "ESC 1", 0)
        self.esc2_vals = self._esc_table(right, "ESC 2", 1)

    def _esc_table(self, parent, title: str, row: int) -> dict:
        fr = tk.Frame(parent, bg=PANEL_BG)
        fr.grid(row=row, column=0, pady=(0, 8), sticky="nw")
        tk.Label(fr, text=title, bg=HDR_BG, font=("Arial", 10, "bold"),
                 bd=1, relief="solid").grid(row=0, column=0, columnspan=4, sticky="ew")
        pairs = [("Pin", "Pout"), ("Vin", "Vout"),
                 ("Iin", "Iout"), ("Whin", "Whout"), ("ERPM", "RPM")]
        vals: dict = {}
        for r, (l1, l2) in enumerate(pairs, start=1):
            for col_off, lbl_text in enumerate([l1, l2]):
                cl = col_off * 2
                cv = col_off * 2 + 1
                tk.Label(fr, text=lbl_text, bg=HDR_BG, font=("Arial", 9, "bold"),
                         bd=1, relief="solid", width=5
                         ).grid(row=r, column=cl, sticky="nsew")
                v = tk.Label(fr, text="--", bg=BOX_BG, font=("Arial", 9),
                             bd=1, relief="solid", width=6)
                v.grid(row=r, column=cv, sticky="nsew")
                vals[lbl_text] = v
        return vals

    # ═══════════════════════════════════════════════════════════════════════════
    # Boucle principale
    # ═══════════════════════════════════════════════════════════════════════════

    def _tick(self):
        self._process_queue()
        self._update_clock()
        self._update_timer_label()
        self.root.after(100, self._tick)

    def _process_queue(self):
        while not self.queue.empty():
            try:
                msg_type, payload = self.queue.get_nowait()
                if msg_type == "data":
                    self._apply_data(payload)
                elif msg_type == "lost":
                    self._set_state("LOST COM", RED)
            except queue.Empty:
                break

    # ═══════════════════════════════════════════════════════════════════════════
    # Application des données reçues
    # ═══════════════════════════════════════════════════════════════════════════

    def _apply_data(self, d: dict):
        self.connected = True

        # JSON capteurs {"type":"sensors", ...}
        if d.get("type") == "sensors":
            self._apply_sensors(d)
            return

        # JSON ESC du Teensy — doit avoir "id" = 10 ou 11
        mid = d.get("id")
        if mid not in (10, 11):
            return

        # Stocker dans le cache par ESC
        self._esc[mid] = d

        # ── Températures (clés exactes du Teensy : "tempmos" et "tempmot") ──
        tempmos = d.get("tempmos")   # température MOSFET
        tempmot = d.get("tempmot")   # température moteur
        if mid == 10:
            if tempmos is not None:
                self.lbl_temp_m1.config(text=f"{tempmos:.1f}°C")
                self.lbl_m_m1.config(text=f"{tempmos:.1f}°C")
            if tempmot is not None:
                self.lbl_m_mot1.config(text=f"{tempmot:.1f}°C")
        else:  # mid == 11
            if tempmos is not None:
                self.lbl_temp_m2.config(text=f"{tempmos:.1f}°C")
                self.lbl_m_m2.config(text=f"{tempmos:.1f}°C")
            if tempmot is not None:
                self.lbl_m_mot2.config(text=f"{tempmot:.1f}°C")

        # Table ESC
        target = self.esc1_vals if mid == 10 else self.esc2_vals
        self._update_esc_table(target, d)

        # Valeurs dérivées recalculées depuis les deux caches ESC
        self._refresh_derived()

    def _apply_sensors(self, d: dict):
        """Données IMU / sonars / servos / GPS — JSON {"type":"sensors", ...}"""
        def _upd(lbl, val, fmt):
            if val is not None:
                lbl.config(text=fmt.format(val))

        _upd(self.lbl_roll,      d.get("roll"),        "{:.2f}°")
        _upd(self.lbl_pitch,     d.get("pitch"),       "{:.2f}°")
        _upd(self.lbl_yaw,       d.get("yaw"),         "{:.2f}°")
        _upd(self.lbl_son_avant, d.get("sonar_avant"), "{:.2f}cm")
        _upd(self.lbl_son_arga,  d.get("sonar_arga"),  "{:.2f}cm")
        _upd(self.lbl_son_ardr,  d.get("sonar_ardr"),  "{:.2f}cm")
        _upd(self.lbl_srv_avant, d.get("servo_avant"), "{:.2f}°")
        _upd(self.lbl_srv_arga,  d.get("servo_arga"),  "{:.2f}°")
        _upd(self.lbl_srv_ardr,  d.get("servo_ardr"),  "{:.2f}°")

        spd = d.get("speed_kmh")
        if spd is not None:
            self.lbl_speed.config(text=f"{spd:.1f} km/h".replace(".", ","))

        state = d.get("state")
        if state is not None:
            color = GREEN if state == "RUN" else RED
            self._set_state(state, color)

    def _refresh_derived(self):
        """Calcule puissance totale, batterie%, efficacité depuis les caches ESC."""
        e10 = self._esc.get(10, {})
        e11 = self._esc.get(11, {})

        # Puissance de sortie totale (W → kW)
        pout_total = (e10.get("pout", 0.0) + e11.get("pout", 0.0)) / 1000.0
        self.lbl_pout.config(text=f"Pout : {pout_total:.2f} kW")

        # Puissance d'entrée (ce que la batterie fournit)
        pin_total = (e10.get("pin", 0.0) + e11.get("pin", 0.0)) / 1000.0
        for lbl in [self.lbl_bat_output, self.lbl_m_bat_output]:
            lbl.config(text=f"Output : {pin_total:.2f} kW")

        # Efficacité moyenne des deux ESC (valeur 0-1 → affichée en %)
        eff_vals = [e.get("eff") for e in [e10, e11] if e.get("eff") is not None]
        if eff_vals:
            avg_eff = sum(eff_vals) / len(eff_vals) * 100.0
            self.lbl_eff.config(text=f"Efficacité : {avg_eff:.1f}%")

        # Batterie % estimée depuis tension (ESC 10 prioritaire)
        vin = e10.get("vin") or e11.get("vin")
        if vin is not None:
            pct = max(0.0, min(100.0, (vin - V_EMPTY) / (V_FULL - V_EMPTY) * 100.0))
            kwh = (pct / 100.0) * BAT_CAPACITY * vin / 1000.0

            if pin_total > 0.05:
                h_rem = int(kwh / pin_total)
                m_rem = int((kwh / pin_total - h_rem) * 60)
                remain_str = f"{h_rem}h{m_rem:02d}m restant"
            else:
                remain_str = "-- restant"

            # Mise à jour directe — pas de fonction imbriquée !
            self.lbl_bat_charge.config(text=f"Charge : {round(pct)}%")
            self.lbl_m_bat_charge.config(text=f"Charge : {round(pct)}%")
            self.lbl_bat_kwh.config(text=f"{kwh:.2f} kWh")
            self.lbl_m_bat_kwh.config(text=f"{kwh:.2f} kWh")
            self.lbl_bat_remain.config(text=remain_str)
            self.lbl_m_bat_remain.config(text=remain_str)

            self._draw_battery_bars(pct)

    def _update_esc_table(self, vals: dict, d: dict):
        """
        Mapping clés Teensy → colonnes du tableau ESC.
        Clés envoyées par le Teensy : pin, pout, vin, vout, iin, iout, whin, whout, erpm, rpm
        """
        mapping = {
            "Pin":   d.get("pin"),
            "Pout":  d.get("pout"),
            "Vin":   d.get("vin"),
            "Vout":  d.get("vout"),
            "Iin":   d.get("iin"),
            "Iout":  d.get("iout"),
            "Whin":  d.get("whin"),
            "Whout": d.get("whout"),
            "ERPM":  d.get("erpm"),
            "RPM":   d.get("rpm"),
        }
        for key, lbl_widget in vals.items():
            v = mapping.get(key)
            if v is None:
                text = "--"
            elif isinstance(v, float):
                text = f"{v:.1f}"
            else:
                text = str(int(v))
            lbl_widget.config(text=text)   # appel direct, pas de fonction imbriquée

    def _set_state(self, text: str, color: str):
        state_text = f"STATE :   {text}"
        self.lbl_state_p.config(text=state_text, fg=color)
        self.lbl_state_c.config(text=state_text, fg=color)

    def _draw_battery_bars(self, pct: float):
        filled = round(pct / 20)
        for canvas, bars in [(self.bat_canvas, self._bat_bars),
                              (self.bat_canvas2, self._bat_bars2)]:
            for i, rect in enumerate(bars):
                canvas.itemconfig(rect, fill=GREEN if i < filled else "#cccccc")

    # ═══════════════════════════════════════════════════════════════════════════
    # Horloge & Timer
    # ═══════════════════════════════════════════════════════════════════════════

    def _update_clock(self):
        t = time.strftime("%H:%M:%S")
        # Appels directs sur chaque label — les fonctions imbriquées ne s'appellent pas toutes seules !
        self.clk_setup.config(text=t)
        self.clk_principal.config(text=t)
        self.clk_controle.config(text=t)
        self.clk_moteur.config(text=t)

    def _toggle_timer(self):
        if self.timer_running:
            self.timer_elapsed += time.time() - self.timer_start
            self.timer_running  = False
            self.btn_timer_onoff.config(bg=BTN_BG, fg="black")
        else:
            self.timer_start   = time.time()
            self.timer_running = True
            self.btn_timer_onoff.config(bg=GREEN, fg="white")

    def _reset_timer(self):
        self.timer_running = False
        self.timer_elapsed = 0.0
        self.timer_start   = 0.0
        self.btn_timer_onoff.config(bg=BTN_BG, fg="black")
        self.lbl_timer.config(text="00:00:00")

    def _update_timer_label(self):
        elapsed = self.timer_elapsed
        if self.timer_running:
            elapsed += time.time() - self.timer_start
        h = int(elapsed) // 3600
        m = (int(elapsed) % 3600) // 60
        s = int(elapsed) % 60
        self.lbl_timer.config(text=f"{h:02d}:{m:02d}:{s:02d}")

    # ═══════════════════════════════════════════════════════════════════════════
    # Communication série
    # ═══════════════════════════════════════════════════════════════════════════

    def _start_serial(self):
        if DEMO_MODE:
            threading.Thread(target=self._demo_loop, daemon=True).start()
        else:
            try:
                self.ser = serial.Serial(SERIAL_PORT, BAUD, timeout=1)
                threading.Thread(target=self._serial_loop,   daemon=True).start()
                threading.Thread(target=self._watchdog_loop, daemon=True).start()
            except Exception as e:
                print(f"[SERIAL] Erreur ouverture {SERIAL_PORT}: {e}")
                print("[SERIAL] Démarrage mode démo automatique")
                threading.Thread(target=self._demo_loop, daemon=True).start()

    def _serial_loop(self):
        while True:
            try:
                line = self.ser.readline().decode(errors="ignore").strip()
                if not line:
                    continue
                data = json.loads(line)
                self.last_msg = time.time()
                self.queue.put(("data", data))
            except json.JSONDecodeError:
                pass   # ligne malformée, ignorée
            except Exception:
                pass

    def _watchdog_loop(self):
        while True:
            if time.time() - self.last_msg > WATCHDOG_SEC:
                self.queue.put(("lost", None))
            time.sleep(0.5)

    def _send_command(self, cmd: str):
        if not DEMO_MODE and hasattr(self, "ser"):
            try:
                self.ser.write((cmd + "\n").encode())
            except Exception as e:
                print(f"[SERIAL] Erreur envoi: {e}")
        else:
            print(f"[CMD démо] {cmd}")

    # ═══════════════════════════════════════════════════════════════════════════
    # Mode démo — simule exactement le format JSON du Teensy
    # ═══════════════════════════════════════════════════════════════════════════

    def _demo_loop(self):
        import math, random
        t = 0.0
        while True:
            t += 0.2

            # Simule les deux ESC avec les VRAIS noms de champs du Teensy
            for esc_id, phase in [(10, 0.0), (11, 0.3)]:
                vin   = 48.2 + 0.5 * math.sin(t * 0.05)
                iin   = 5.2  + random.uniform(-0.3, 0.3)
                duty  = 0.79 + 0.02 * math.sin(t * 0.2 + phase)
                vout  = vin * duty
                iout  = 42.0 + 2 * math.sin(t * 0.3 + phase) + random.uniform(-0.5, 0.5)
                pin   = vin * iin
                pout  = vout * iout
                eff   = pout / pin if pin > 0 else 0
                whin  = 5.0 + t * 0.002
                whout = whin * eff
                erpm  = 9600 + 100 * math.sin(t * 0.4 + phase)
                rpm   = erpm / 3.0

                self.queue.put(("data", {
                    "id":     esc_id,
                    "vin":    round(vin, 2),
                    "iin":    round(iin, 2),
                    "pin":    round(pin, 1),
                    "ahin":   round(t * 0.001, 4),
                    "whin":   round(whin, 3),
                    "vout":   round(vout, 2),
                    "iout":   round(iout, 2),
                    "pout":   round(pout, 1),
                    "ahout":  round(t * 0.0009, 4),
                    "whout":  round(whout, 3),
                    "effwh":  round(eff, 3),
                    "eff":    round(eff, 3),
                    "rpm":    round(rpm, 1),
                    "erpm":   round(erpm, 1),
                    "duty":   round(duty, 3),
                    "tach":   round(t * 500, 1),
                    "tempmot": round(26.0 + 2 * math.sin(t * 0.04 + phase) + random.uniform(-0.2, 0.2), 1),
                    "tempmos": round(26.5 + 1.5 * math.sin(t * 0.05 + phase) + random.uniform(-0.2, 0.2), 1),
                }))

            # Simule le JSON capteurs (à implémenter sur le Teensy)
            self.queue.put(("data", {
                "type":        "sensors",
                "speed_kmh":   round(18.1 + 2 * math.sin(t * 0.3), 1),
                "roll":        round(26.51 + random.uniform(-0.5, 0.5), 2),
                "pitch":       round(54.19 + random.uniform(-0.5, 0.5), 2),
                "yaw":         round(-26.22 + random.uniform(-0.3, 0.3), 2),
                "sonar_avant": round(54.31 + random.uniform(-0.5, 0.5), 2),
                "sonar_arga":  round(54.19 + random.uniform(-0.5, 0.5), 2),
                "sonar_ardr":  round(53.87 + random.uniform(-0.5, 0.5), 2),
                "servo_avant": round(0.62 + random.uniform(-0.1, 0.1), 2),
                "servo_arga":  round(-2.98 + random.uniform(-0.1, 0.1), 2),
                "servo_ardr":  round(5.71 + random.uniform(-0.1, 0.1), 2),
                "state":       "RUN",
            }))

            time.sleep(0.2)


# ═══════════════════════════════════════════════════════════════════════════════
if __name__ == "__main__":
    root = tk.Tk()
    app  = HydrofoilDashboard(root)
    root.mainloop()
