from PySide6.QtCore import QObject, Signal

from Models.pid_config import PIDConfig

class TuningViewModel(QObject):
    config_changed = Signal()

    def __init__(self):
        super().__init__()
        self._config = PIDConfig.defaults()

    def get(self, axe: str, gain: str) -> float:
        return getattr(getattr(self._config, axe), gain)

    def set(self, axe: str, gain: str, valeur: float):
        setattr(getattr(self._config, axe), gain, valeur)
        self.config_changed.emit()

    def reset(self):
        self._config = PIDConfig.defaults()
        self.config_changed.emit()