from ..qt import *

class DistortionTextEdit(QTextEdit):
  def __init__(self):
    super().__init__()
    self.on_focus_out = lambda: None

  def focusOutEvent(self, e):
    self.on_focus_out()

class DistortionWidget(QWidget):
  def __init__(self, config, stub):
    super().__init__()
    self.config = config
    self.stub = stub

    if 'Threshold' not in config:
      config['Threshold'] = 100
    if 'Show Threshold' not in config:
      config['Show Threshold'] = False
    if 'Rows' not in config:
      config['Rows'] = 7
    if 'Columns' not in config:
      config['Columns'] = 8
    if 'Collecting' not in config:
      config['Collecting'] = False
    if 'Invert' not in config:
      config['Invert'] = False

    if 'Camera Matrix' not in config:
      config['Camera Matrix'] = [[1.0, 0.0, 0.0],
                                 [0.0, 1.0, 0.0],
                                 [0.0, 0.0, 1.0]]

    if 'Distortion Coefficients' not in config:
      config['Distortion Coefficients'] = [0.0, 0.0, 0.0, 0.0, 0.0]

    config.add_observer(self.on_change, lambda: isdeleted(self))

    def update_camera_matrix(a, k, v):
      print('update_camera_matrix', k, v)
      self.on_change(None, 'Camera Matrix', config['Camera Matrix'])
    def update_distortion_coefficients(a, k, v):
      print('update_distortion_coefficients', k, v)
      self.on_change(None, 'Distortion Coefficients', config['Distortion Coefficients'])

    config['Camera Matrix'].add_observer(update_camera_matrix, lambda: isdeleted(self))
    config['Camera Matrix'][0].add_observer(update_camera_matrix, lambda: isdeleted(self))
    config['Camera Matrix'][1].add_observer(update_camera_matrix, lambda: isdeleted(self))
    config['Camera Matrix'][2].add_observer(update_camera_matrix, lambda: isdeleted(self))

    config['Distortion Coefficients'].add_observer(update_distortion_coefficients, lambda: isdeleted(self))

    layout = QVBoxLayout()

    self.collecting_checkbox = QCheckBox('Collecting')
    self.collecting_checkbox.toggled.connect(lambda value: config.update({'Collecting': value}))
    layout.addWidget(self.collecting_checkbox)

    self.invert_checkbox = QCheckBox('Invert')
    self.invert_checkbox.toggled.connect(lambda value: config.update({'Invert': value}))
    layout.addWidget(self.invert_checkbox)

    self.show_threshold = QCheckBox('Show Threshold')
    self.show_threshold.toggled.connect(lambda value: config.update({'Show Threshold': value}))
    layout.addWidget(self.show_threshold)

    layout.addWidget(QLabel('Threshold:'))
    self.threshold_slider = QSlider(Qt.Orientation.Horizontal)
    self.threshold_slider.setRange(0, 255)
    self.threshold_slider.valueChanged.connect(lambda value: config.update({'Threshold': value}))
    layout.addWidget(self.threshold_slider)

    layout.addWidget(QLabel('Rows:'))
    self.rows_spinbox = QSpinBox()
    self.rows_spinbox.valueChanged.connect(lambda value: config.update({'Rows': value}))
    layout.addWidget(self.rows_spinbox)

    layout.addWidget(QLabel('Columns:'))
    self.columns_spinbox = QSpinBox()
    self.columns_spinbox.valueChanged.connect(lambda value: config.update({'Columns': value}))
    layout.addWidget(self.columns_spinbox)

    layout.addWidget(QLabel('Distortion Coefficients:'))
    self.distortion_edit = QLineEdit()
    self.distortion_edit.editingFinished.connect(lambda: config.update({'Distortion Coefficients': [float(s) for s in self.distortion_edit.text().split(' ') if s]}))
    layout.addWidget(self.distortion_edit)

    def on_matrix_changed():
      text = self.camera_matrix_edit.toPlainText()

      try:
        matrix = [[float(token) for token in line.split(' ') if token] for line in text.split('\n')]
        matrix_is_bad = len(matrix) != 3 or not all(len(line) == 3 for line in matrix)
      except ValueError:
        matrix_is_bad = True

      if matrix_is_bad:
        new_text = '\n'.join(' '.join(str(t) for t in line) for line in config['Camera Matrix'])
        self.camera_matrix_edit.setPlainText(new_text)
        return
      config['Camera Matrix'] = matrix

    layout.addWidget(QLabel('Camera Matrix:'))
    self.camera_matrix_edit = DistortionTextEdit()
    self.camera_matrix_edit.on_focus_out = on_matrix_changed
    layout.addWidget(self.camera_matrix_edit)

    layout.addStretch(1)
    self.setLayout(layout)
    for k, v in self.config.items():
      self.on_change(None, k, v)

  def on_change(self, action, key, value):
    print('DistortionWidget', key, value)
    if key == 'Invert':
      if self.invert_checkbox.isChecked() != value:
        self.invert_checkbox.setChecked(value)
    elif key == 'Show Threshold':
      if self.show_threshold.isChecked() != value:
        self.show_threshold.setChecked(value)
    elif key == 'Collecting':
      if self.collecting_checkbox.isChecked() != value:
        self.collecting_checkbox.setChecked(value)
    elif key == 'Threshold':
      if self.threshold_slider.value() != value:
        self.threshold_slider.setValue(value)
    elif key == 'Rows':
      if self.rows_spinbox.value() != value:
        self.rows_spinbox.setValue(value)
    elif key == 'Columns':
      if self.columns_spinbox.value() != value:
        self.columns_spinbox.setValue(value)
    elif key == 'Camera Matrix':
      unparsed = '\n'.join([' '.join([str(v) for v in row]) for row in value])
      if unparsed != self.camera_matrix_edit.toPlainText():
        self.camera_matrix_edit.setPlainText(unparsed)
    elif key == 'Distortion Coefficients':
      unparsed = ' '.join([str(v) for v in value])
      if unparsed != self.distortion_edit.text():
        self.distortion_edit.setText(unparsed)

