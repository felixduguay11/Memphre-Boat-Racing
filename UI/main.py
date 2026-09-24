import sys
from PySide6.QtWidgets import QApplication, QWidget

from View.tuning_view import TuningView

app = QApplication(sys.argv)
w = TuningView()
w.setWindowTitle("NomApplication")
w.resize(800, 480)
w.show()
sys.exit(app.exec())
