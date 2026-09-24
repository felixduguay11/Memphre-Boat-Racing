from dataclasses import dataclass, field

@dataclass
class AxisGain:

    kp: float = 0.0
    ki: float = 0.0
    kd: float = 0.0

@dataclass
class PIDConfig:
    hauteur : AxisGain = field(default_factory=AxisGain)
    roll : AxisGain = field(default_factory=AxisGain)
    pitch : AxisGain = field(default_factory=AxisGain)

    @staticmethod
    def default() -> "PIDConfig":
        """Valeurs des tunings de PID"""
        return PIDConfig(
            hauteur=AxisGain(kp=0.1, ki=0.1, kd=0.1),
            roll=AxisGain(kp=0.1, ki=0.1, kd=0.1),
            pitch=AxisGain(kp=0.1, ki=0.1, kd=0.1),
        )


