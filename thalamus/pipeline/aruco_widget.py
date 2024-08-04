from ..qt import *

class ArucoWidget(QWidget):
  def __init__(self, config, stub):
    super().__init__()

    config['Boards'] = [{
      'Rows': 4,
      'Columns': 3,
      'Marker Size': .039,
      'Marker Separation': .013,
    }]
