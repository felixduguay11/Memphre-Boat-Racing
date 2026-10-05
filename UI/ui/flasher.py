# -*- coding: utf-8 -*-
"""
flasher.py — compile et televerse le firmware du Teensy depuis l'UI.

Trois etapes, lancees avec QProcess (l'UI reste fluide pendant ce temps) :
  1. "git pull --ff-only"  recupere le dernier code (si GIT_PULL)
  2. "pio run"             compilation, le lien serie reste ouvert
  3. "pio run -t upload"   televersement, le port doit etre libere avant

Si le pull ou la compilation echoue, on s'arrete la : le Teensy n'a pas
ete touche et le lien serie n'a jamais ete coupe.

MODE HEX (hex_path non vide) — Integration taille reel :
  Le firmware FreeRTOS utilise la plateforme tsandmann, dont la toolchain
  n'existe PAS pour le processeur ARM du Pi : "pio run" y echoue. Le .hex
  est donc compile sur PC (copie automatique dans firmware/ par
  scripts/copie_hex.py), committe, puis ici :
  1. "git pull --ff-only"    recupere le dernier firmware.hex (si GIT_PULL)
  2. reboot en bootloader     ouverture du port a 134 bauds (Teensy USB)
  3. "teensy_loader_cli"      televersement du .hex

--ff-only : le pull n'avance que si c'est une simple mise a jour. Une
modification locale sur le Pi ou un historique diverge le fait echouer
au lieu de creer un merge que personne ne verra sur l'ecran.
"""

import os
import re
import shutil

from PySide6.QtCore import QObject, QProcess, QProcessEnvironment, QTimer, Signal

try:
    import serial
except ImportError:
    serial = None

_ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def find_pio(configured: str) -> str:
    """pio du PATH, sinon l'installation standard de PlatformIO.
    Le PATH d'un lancement au boot (systemd) ne contient souvent pas
    ~/.platformio, d'ou le chemin complet en repli."""
    for p in (configured, shutil.which("pio"),
              os.path.expanduser("~/.platformio/penv/bin/pio")):
        if p and os.path.isfile(os.path.expanduser(p)):
            return os.path.expanduser(p)
    return ""


def find_teensy_cli(configured: str) -> str:
    """teensy_loader_cli configure, sinon celui du PATH (apt), sinon
    celui compile dans ~/teensy_loader_cli."""
    for p in (configured, shutil.which("teensy_loader_cli"),
              os.path.expanduser("~/teensy_loader_cli/teensy_loader_cli")):
        if p and os.path.isfile(os.path.expanduser(p)):
            return os.path.expanduser(p)
    return ""


# Delai max pour trouver le Teensy en mode programmation
HEX_TIMEOUT_MS = 60000


class TeensyFlasher(QObject):
    output = Signal(str)          # une ligne de sortie de pio
    state = Signal(str)           # etape en cours, pour l'utilisateur
    finished = Signal(bool, str)  # succes, message final

    def __init__(self, fw_dir: str, pio: str = "", release_port=None,
                 git_pull: bool = True, hex_path: str = "",
                 serial_port: str = "", teensy_cli: str = ""):
        super().__init__()
        self._fw_dir = os.path.expanduser(fw_dir)
        self._pio_cfg = pio
        self._git_pull = git_pull
        self._pio = ""
        # Mode hex (voir en-tete)
        self._hex = os.path.expanduser(hex_path) if hex_path else ""
        self._port = serial_port
        self._cli_cfg = teensy_cli
        self._cli = ""
        self._timed_out = False
        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(self._on_timeout)
        self._release_port = release_port or (lambda: None)
        self._phase = None
        self._proc = QProcess(self)
        self._proc.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
        self._proc.readyReadStandardOutput.connect(self._read)
        self._proc.finished.connect(self._on_finished)
        self._proc.errorOccurred.connect(self._on_error)

    @property
    def busy(self) -> bool:
        return self._phase is not None

    def start(self):
        if self.busy:
            return
        if self._hex:
            tool = find_teensy_cli(self._cli_cfg)
            if not tool:
                self.finished.emit(False, "teensy_loader_cli introuvable : "
                                          "sudo apt install teensy-loader-cli")
                return
            if not os.path.isdir(self._fw_dir):
                self.finished.emit(False, "dossier introuvable : %s" % self._fw_dir)
                return
            self._cli = tool
        else:
            tool = find_pio(self._pio_cfg)
            if not tool:
                self.finished.emit(False, "pio introuvable : installer PlatformIO Core")
                return
            if not os.path.isfile(os.path.join(self._fw_dir, "platformio.ini")):
                self.finished.emit(False, "pas de platformio.ini dans %s" % self._fw_dir)
                return
            self._pio = tool

        env = QProcessEnvironment.systemEnvironment()
        env.insert("PATH", os.path.dirname(tool) + os.pathsep + env.value("PATH"))
        # Jamais de question interactive : personne ne peut repondre a
        # une demande de mot de passe depuis l'ecran tactile.
        env.insert("GIT_TERMINAL_PROMPT", "0")
        env.insert("GIT_SSH_COMMAND", "ssh -o BatchMode=yes")
        self._proc.setProcessEnvironment(env)
        self._proc.setWorkingDirectory(self._fw_dir)

        if self._git_pull:
            git = shutil.which("git")
            if not git:
                self.finished.emit(False, "git introuvable : sudo apt install git")
                return
            self._run("pull", git, ["pull", "--ff-only"], "Recuperation du code (git pull)...")
        else:
            self._build()

    # ------------------------------------------------------------ interne
    def _run(self, phase, program, args, label):
        self._phase = phase
        self.state.emit(label)
        self.output.emit("$ %s %s" % (os.path.basename(program), " ".join(args)))
        self._proc.setProgram(program)
        self._proc.setArguments(args)
        self._proc.start()

    def _build(self):
        if self._hex:
            self._upload_hex()
            return
        self._run("build", self._pio, ["run"], "Compilation...")

    # ------------------------------------------------------------ mode hex
    def _upload_hex(self):
        if not os.path.isfile(self._hex):
            self._end(False, "firmware.hex absent (%s) : compiler sur PC et "
                             "committer firmware/firmware.hex" % self._hex)
            return
        self._release_port()
        self._reboot_bootloader()
        self._timed_out = False
        self._timer.start(HEX_TIMEOUT_MS)
        self._run("upload_hex", self._cli,
                  ["--mcu=TEENSY41", "-w", "-v", self._hex],
                  "Televersement... (bouton blanc du Teensy si rien ne bouge)")

    def _reboot_bootloader(self):
        """Ouvrir le port USB du Teensy a 134 bauds le fait redemarrer en
        mode programmation (gere par le core Teensy, meme sous FreeRTOS)."""
        if serial is None or not self._port:
            self.output.emit("reboot auto impossible (pyserial/port) : "
                             "appuyer sur le bouton blanc du Teensy")
            return
        try:
            s = serial.Serial(self._port, 134)
            s.close()
            self.output.emit("Teensy redemarre en mode programmation")
        except Exception as e:
            self.output.emit("reboot auto impossible (%s) : "
                             "appuyer sur le bouton blanc du Teensy" % e)

    def _on_timeout(self):
        if self._phase == "upload_hex":
            self._timed_out = True
            self._proc.kill()

    def _read(self):
        data = bytes(self._proc.readAllStandardOutput()).decode("utf-8", "replace")
        for line in _ANSI.sub("", data).splitlines():
            if line.strip():
                self.output.emit(line)

    def _on_finished(self, code, status):
        ok = status == QProcess.ExitStatus.NormalExit and code == 0
        if self._phase == "pull":
            if not ok:
                self._end(False, "git pull echoue (reseau ? modif locale ?) : "
                                 "Teensy non modifie")
                return
            self._build()
        elif self._phase == "build":
            if not ok:
                self._end(False, "Compilation echouee : Teensy non modifie")
                return
            self._release_port()
            self._run("upload", self._pio, ["run", "-t", "upload"],
                      "Televersement... (bouton blanc du Teensy si rien ne bouge)")
        elif self._phase == "upload":
            if ok:
                self._end(True, "Teensy a jour")
            else:
                self._end(False, "Televersement echoue (code %d)" % code)
        elif self._phase == "upload_hex":
            self._timer.stop()
            if ok:
                self._end(True, "Teensy a jour")
            elif self._timed_out:
                self._end(False, "Teensy introuvable en mode programmation : "
                                 "appuyer sur le bouton blanc puis relancer")
            else:
                self._end(False, "Televersement echoue (code %d)" % code)

    def _on_error(self, err):
        # FailedToStart n'emet pas finished : on conclut ici.
        if err == QProcess.ProcessError.FailedToStart and self.busy:
            self._timer.stop()
            self._end(False, "impossible de lancer %s" %
                      os.path.basename(self._proc.program()))

    def _end(self, ok, msg):
        self._phase = None
        self.finished.emit(ok, msg)
