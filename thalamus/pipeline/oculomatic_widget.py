from ..qt import *

class OculomaticWidget(QWidget):
  def __init__(self, config, stub):
    super().__init__()
    self.config = config
    self.stub = stub

    if 'Computing' not in config:
      config['Computing'] = False
    if 'Threshold' not in config:
      config['Threshold'] = 100
    if 'Min Area' not in config:
      config['Min Area'] = 0
    if 'Max Area' not in config:
      config['Max Area'] = 100
    if 'X Gain' not in config:
      config['X Gain'] = 0.0
    if 'Y Gain' not in config:
      config['Y Gain'] = 0.0

    if 'Invert X' not in config:
      config['Invert X'] = False
    if 'Invert Y' not in config:
      config['Invert Y'] = False

    config.add_observer(self.on_change, lambda: isdeleted(self))

    layout = QVBoxLayout()

    self.running_checkbox = QCheckBox('Computing')
    self.running_checkbox.toggled.connect(lambda value: config.update({'Computing': value}))
    layout.addWidget(self.running_checkbox)

    layout.addWidget(QLabel('Threshold:'))
    self.threshold_slider = QSlider(Qt.Orientation.Horizontal)
    self.threshold_slider.setRange(0, 255)
    self.threshold_slider.valueChanged.connect(lambda value: config.update({'Threshold': value}))
    layout.addWidget(self.threshold_slider)

    layout.addWidget(QLabel('Max Area:'))
    self.max_area_slider = QSlider(Qt.Orientation.Horizontal)
    self.max_area_slider.setRange(0, 100)
    self.max_area_slider.valueChanged.connect(lambda value: config.update({'Max Area': value}))
    layout.addWidget(self.max_area_slider)

    layout.addWidget(QLabel('Min Area:'))
    self.min_area_slider = QSlider(Qt.Orientation.Horizontal)
    self.min_area_slider.setRange(0, 500)
    self.min_area_slider.valueChanged.connect(lambda value: config.update({'Min Area': value}))
    layout.addWidget(self.min_area_slider)

    layout.addWidget(QLabel('X Gain:'))
    self.x_gain_slider = QSlider(Qt.Orientation.Horizontal)
    self.x_gain_slider.setRange(0, 500)
    self.x_gain_slider.valueChanged.connect(lambda value: config.update({'X Gain': float(value)}))
    layout.addWidget(self.x_gain_slider)

    layout.addWidget(QLabel('Y Gain:'))
    self.y_gain_slider = QSlider(Qt.Orientation.Horizontal)
    self.y_gain_slider.setRange(0, 500)
    self.y_gain_slider.valueChanged.connect(lambda value: config.update({'Y Gain': float(value)}))
    layout.addWidget(self.y_gain_slider)

    self.invert_x_checkbox = QCheckBox('Invert X')
    self.invert_x_checkbox.toggled.connect(lambda value: config.update({'Invert X': value}))
    layout.addWidget(self.invert_x_checkbox)

    self.invert_y_checkbox = QCheckBox('Invert Y')
    self.invert_y_checkbox.toggled.connect(lambda value: config.update({'Invert Y': value}))
    layout.addWidget(self.invert_y_checkbox)

    layout.addStretch(1)

    self.setLayout(layout)

    for k, v in self.config.items():
      self.on_change(None, k, v)
                                          
  def on_change(self, action, key, value):
    if key == 'Computing':
      if self.running_checkbox.isChecked() != value:
        self.running_checkbox.setChecked(value)
    elif key == 'Invert X':
      if self.invert_x_checkbox.isChecked() != value:
        self.invert_x_checkbox.setChecked(value)
    elif key == 'Invert Y':
      if self.invert_y_checkbox.isChecked() != value:
        self.invert_y_checkbox.setChecked(value)
    elif key == 'Threshold':
      if self.threshold_slider.value() != value:
        self.threshold_slider.setValue(value)
    elif key == 'Min Area':
      if self.min_area_slider.value() != value:
        self.min_area_slider.setValue(value)
    elif key == 'Max Area':
      if self.max_area_slider.value() != value:
        self.max_area_slider.setValue(value)
    elif key == 'X Gain':
      if self.x_gain_slider.value() != value:
        self.x_gain_slider.setValue(value)
    elif key == 'Y Gain':
      if self.y_gain_slider.value() != value:
        self.y_gain_slider.setValue(value)

