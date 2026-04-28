"""
Implementation of the simple task
"""
import math
import typing
import logging
import datetime
import typing_extensions
import asyncio

import numpy
import scipy.signal

from ..qt import *

from . import task_context
from .widgets import Form, ListAsTabsWidget
from .util import wait_for, wait_for_hold, TaskResult, TaskContextProtocol, CanvasPainterProtocol, RenderOutput, create_task_with_exc_handling
from .. import task_controller_pb2
from ..config import *

from ..thalamus_pb2 import StimRequest, StimDeclaration, AnalogResponse, Span

LOGGER = logging.getLogger(__name__)

Config = typing.NamedTuple('Config', [
  ('intertrial_timeout', datetime.timedelta),
  ('start_timeout', datetime.timedelta),
  ('hold_timeout', datetime.timedelta),
  ('blink_timeout', datetime.timedelta),
  ('fail_timeout', datetime.timedelta),
  ('success_timeout', datetime.timedelta),
  ('target_rectangle', QRect),
  ('target_color', QColor),
])

RANDOM_DEFAULT = {'min': 1, 'max':1}
COLOR_DEFAULT = [255, 255, 255]

def compute_waveform(config: ObservableDict):
  try:
    stimulation_channel = config['Stimulation Channel']

    if stimulation_channel == "AO3":
        dc_off = 13e-4
    elif stimulation_channel == "AO2":
        dc_off = 30e-4
    elif stimulation_channel == "AO1":
        dc_off = 10e-4
    elif stimulation_channel == "AO0":
        dc_off = 20e-4
    else:
        dc_off = 0

    sample_rate_hz = 125e3
    sample_period_s = 1/sample_rate_hz
    amplitude_ua = config['Amplitude (uA)']
    amplitude_a = amplitude_ua/1e6
    amplitude_v = amplitude_a*1e4
    pulse_width_us = config['Pulse Width (us)']
    pulse_width_s = pulse_width_us/1e6
    pulse_width_samples = int(pulse_width_s*sample_rate_hz)
    if pulse_width_samples == 0:
      return
    pulse_width_hz = 1/(2*pulse_width_s)
    frequency_hz = config['Frequency (Hz)']
    duration_s = 1/frequency_hz
    duration_samples = int(duration_s*sample_rate_hz)
    num_pulses = config['Number of Pulses']
    interphase_delay_ms = config['Interphase Delay (ms)']
    interphase_delay_s = interphase_delay_ms/1e3
    interphase_delay_samples = int(interphase_delay_s*sample_rate_hz)
    stimulation_duration_s = config['Stimulation Duration (s)']
    discharge_duration_s = config['Discharge Duration (s)']
    is_biphasic = config['Phase'] == 'Biphasic'
    polarity = 1 if config['Lead'] == 'Cathode-leading' else -1
    repetitions = int(stimulation_duration_s/duration_s)
    total_samples = int(stimulation_duration_s*sample_rate_hz)
    if repetitions == 0:
      return

    out_wave = numpy.zeros((total_samples,))
    for i in range(0, len(out_wave), duration_samples):
      if is_biphasic:
        for j in range(num_pulses):
          offset = i + 2*j*(pulse_width_samples + interphase_delay_samples)
          end = offset + pulse_width_samples
          if total_samples < end:
            break
          out_wave[offset:end] = polarity*amplitude_v-dc_off
          offset = i + (2*j+1)*(pulse_width_samples + interphase_delay_samples)
          end = offset + pulse_width_samples
          if total_samples < end:
            break
          out_wave[offset:end] = -polarity*amplitude_v-dc_off
      else:
        for j in range(num_pulses):
          offset = i + j*(pulse_width_samples + interphase_delay_samples)
          end = offset + pulse_width_samples
          if total_samples < end:
            break
          out_wave[offset:offset + pulse_width_samples] = polarity*amplitude_v-dc_off

    out_wave[0] = 0
    out_wave[-1] = 0
    return out_wave
    #numpy.savetxt("foo.csv", out_wave)
  except ZeroDivisionError:
    return None

class WaveformWidget(QWidget):
  def __init__(self, config: ObservableDict):
    super().__init__()
    self.config = config
    self.path = None
    self.bounds = 0, 0, 0, 0
    self.last_x = None
    self.zoom = 1
    self.offset = 0

    config.add_recursive_observer(lambda *args: self.update_wave(), lambda: isdeleted(self), True)

  def update_wave(self):
    out_wave = compute_waveform(self.config)
    if out_wave is None:
      return
    
    #self.scales = 1000/(duration_s), 1000/(numpy.max(out_wave)-numpy.min(out_wave))
    self.out_wave = out_wave
    self.path = QPainterPath()
    self.path.moveTo(0, out_wave[0])
    sample_rate_hz = 125e3
    sample_period_s = 1/sample_rate_hz
    for i, sample in enumerate(out_wave):
      #t = i*sample_period_s
      #if i >= cycle_samples:
        #break
      self.path.lineTo(i*sample_period_s, sample)

    self.bounds = 0, numpy.min(out_wave), i*sample_period_s, (numpy.max(out_wave) - numpy.min(out_wave))
    self.update()

  def paintEvent(self, e: QPaintEvent):
    if self.path is None or self.bounds[2] == 0 or self.bounds[3] == 0:
      return

    painter = QPainter(self)
    painter.drawText(QRect(0, 0, self.width(), self.height()), Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop, f'{-self.offset*1000:.3f}ms')
    painter.drawText(QRect(0, 0, self.width(), self.height()), Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignTop, f'{(-self.offset + self.bounds[2]/self.zoom)*1000:.3f}ms')

    pen = painter.pen()
    pen.setCosmetic(True)
    painter.setPen(pen)
    painter.translate(0, .05*self.height())
    painter.scale(1, .9)
    painter.translate(0, self.height())
    painter.scale(self.width()/self.bounds[2], -self.height()/self.bounds[3])
    painter.translate(-self.bounds[0], -self.bounds[1])

    painter.scale(self.zoom, 1)
    painter.translate(self.offset, 0)

    painter.drawPath(self.path)
    #painter.drawEllipse(0, 0, 100, 100)

  def wheelEvent(self, e: QWheelEvent):
    if e.angleDelta().y() > 0:
      self.zoom *= 1.1
    else:
      self.zoom *= .9
      self.zoom = max(self.zoom, 1)
    self.update()

  def mousePressEvent(self, e: QMouseEvent):
    self.last_x = qt_get_x(e)

  def mouseReleaseEvent(self, e: QMouseEvent):
    self.last_x = None

  def mouseMoveEvent(self, e: QMouseEvent):
    if self.last_x is not None:
      new_x = qt_get_x(e)
      self.offset += (new_x - self.last_x)*(self.bounds[2]/self.zoom)/self.width()
      self.last_x = new_x
      self.update()


class StimWidget(QWidget):
  def __init__(self, config: ObservableDict):
    super().__init__()
    layout = QGridLayout()

    def add_switch(field: str, options: typing.List[str]):
      if field not in config:
        config[field] = options[0]

      def on_switch(button: QRadioButton):
        if not button.isChecked():
          return
        
        for option, radio in zip(options, radios):
          if radio is button:
            config[field] = option

      radios = [QRadioButton(option) for option in options]
      group = QButtonGroup(self)
      row = layout.rowCount()
      layout.addWidget(QLabel(f'{field}:'), row, 0)
      for i, radio in enumerate(radios):
        radio.clicked.connect(lambda checked, radio=radio: on_switch(radio))
        group.addButton(radio)
        layout.addWidget(radio, row, i+1)

      def on_config_change(action, key, value):
        if key == field:
          for radio, option in zip(radios, options):
            if value == option:
              radio.setChecked(True)
      config.add_observer(on_config_change, lambda: isdeleted(self), True)

    def add_combo(field: str, options: typing.List[str]):
      if field not in config:
        config[field] = options[0] if options else ''
        
      def on_change(text: str):
        print('on_change', text)
        config[field] = text

      combo = QComboBox()
      combo.addItems(options)
      combo.currentTextChanged.connect(on_change)
      row = layout.rowCount()
      layout.addWidget(QLabel(f'{field}:'), row, 0)
      layout.addWidget(combo, row, 1, 1, 2)

      def on_config_change(action, key, value):
        if key == field:
          combo.setCurrentText(value)
      config.add_observer(on_config_change, lambda: isdeleted(self), True)

      return combo
    
    def add_spinbox(field: str, default_value: float = 0, is_double: bool = True):
      if field not in config:
        config[field] = float(default_value) if is_double else int(default_value)
        
      def on_change(value: typing.Union[int, float]):
        config[field] = value

      spinbox = QDoubleSpinBox() if is_double else QSpinBox()
      spinbox.setMaximum(2**30)
      spinbox.valueChanged.connect(on_change)
      row = layout.rowCount()
      layout.addWidget(QLabel(f'{field}:'), row, 0)
      layout.addWidget(spinbox, row, 1, 1, 2)

      def on_config_change(action, key, value):
        if key == field:
          spinbox.setValue(value)
      config.add_observer(on_config_change, lambda: isdeleted(self), True)

    add_switch('Polarity', ['Monopolar', 'Bipolar'])
    add_combo('Stimulation Channel', ['AO0', 'AO1', 'AO2', 'AO3'])
    return_combo = add_combo('Return Channel', [])
    return_combo.setEnabled(False)
    add_switch('Phase', ['Biphasic', 'Monophasic'])
    add_switch('Lead', ['Cathode-leading', 'Anode-leading'])
    add_spinbox('Amplitude (uA)', 100)
    add_spinbox('Pulse Width (us)', 200)
    add_spinbox('Frequency (Hz)', 200)
    add_spinbox('Number of Pulses', 1, False)
    add_spinbox('Interphase Delay (ms)', 0)
    add_spinbox('Stimulation Duration (s)', .5)
    add_spinbox('Discharge Duration (s)', .5)
    wave = WaveformWidget(config)
    layout.addWidget(wave, 0, layout.columnCount(), layout.rowCount(), 1)

    self.setLayout(layout)


class ElectrodeModel(typing_extensions.Protocol):
  def set_mux(self, mux: int) -> None: ...

class UECog244ChV5Widget(QWidget):
  def __init__(self, mux):
    super().__init__()

    self.nidaq = [
      [   '',    '', 'AO2', 'AO2', 'AO2', 'AO2', 'AO3', 'AO3', 'AO2', 'AO0', 'AO1', 'AO1', 'AO1', 'AO0',    '',    ''],
      [   '', 'AO1', 'AO3', 'AO2', 'AO3', 'AO3', 'AO2', 'AO2', 'AO3', 'AO1', 'AO0', 'AO0', 'AO1', 'AO0', 'AO1',    ''],
      ['AO0', 'AO1', 'AO0', 'AO3', 'AO3', 'AO2', 'AO3', 'AO3', 'AO0', 'AO0', 'AO1', 'AO0', 'AO0', 'AO0', 'AO2', 'AO3'],
      ['AO0', 'AO1', 'AO0', 'AO1', 'AO2', 'AO2', 'AO3', 'AO2', 'AO1', 'AO0', 'AO1', 'AO1', 'AO1', 'AO3', 'AO2', 'AO2'],
      ['AO0', 'AO1', 'AO0', 'AO1', 'AO0', 'AO3', 'AO2', 'AO3', 'AO0', 'AO1', 'AO0', 'AO0', 'AO2', 'AO3', 'AO2', 'AO3'],
      ['AO0', 'AO1', 'AO0', 'AO1', 'AO0', 'AO1', 'AO3', 'AO2', 'AO1', 'AO0', 'AO1', 'AO3', 'AO2', 'AO3', 'AO2', 'AO3'],
      ['AO1', 'AO0', 'AO1', 'AO0', 'AO1', 'AO0', 'AO1', 'AO3', 'AO0', 'AO1', 'AO3', 'AO2', 'AO3', 'AO2', 'AO3', 'AO2'],
      ['AO2', 'AO3', 'AO0', 'AO1', 'AO0', 'AO1', 'AO0', 'AO2', 'AO2', 'AO3', 'AO2', 'AO3', 'AO2', 'AO3', 'AO2', 'AO3'],
      ['AO3', 'AO2', 'AO3', 'AO2', 'AO3', 'AO2', 'AO3', 'AO2', 'AO2', 'AO0', 'AO1', 'AO0', 'AO1', 'AO0', 'AO3', 'AO2'],
      ['AO2', 'AO3', 'AO2', 'AO3', 'AO2', 'AO3', 'AO1', 'AO0', 'AO3', 'AO1', 'AO0', 'AO1', 'AO0', 'AO1', 'AO0', 'AO1'],
      ['AO3', 'AO2', 'AO3', 'AO2', 'AO3', 'AO1', 'AO0', 'AO1', 'AO2', 'AO3', 'AO1', 'AO0', 'AO1', 'AO0', 'AO1', 'AO0'],
      ['AO3', 'AO2', 'AO3', 'AO2', 'AO0', 'AO0', 'AO1', 'AO0', 'AO3', 'AO2', 'AO3', 'AO0', 'AO1', 'AO0', 'AO1', 'AO0'],
      ['AO2', 'AO2', 'AO3', 'AO1', 'AO1', 'AO1', 'AO0', 'AO1', 'AO2', 'AO3', 'AO2', 'AO2', 'AO1', 'AO0', 'AO1', 'AO0'],
      ['AO3', 'AO2', 'AO0', 'AO0', 'AO0', 'AO1', 'AO0', 'AO0', 'AO3', 'AO3', 'AO2', 'AO3', 'AO3', 'AO0', 'AO1', 'AO0'],
      [   '', 'AO1', 'AO0', 'AO1', 'AO0', 'AO0', 'AO1', 'AO3', 'AO2', 'AO2', 'AO3', 'AO3', 'AO2', 'AO3', 'AO1',    ''],
      [   '',    '', 'AO0', 'AO1', 'AO1', 'AO1', 'AO0', 'AO2', 'AO3', 'AO3', 'AO2', 'AO2', 'AO2', 'AO2',    '',    '']
    ]

    self.mux_map = [
      [   -1,    -1,     1,     2,     4,    10,     9,    13,    15,    12,    12,     5,     2,     0,    -1,    -1],
      [   -1,     1,     0,     3,     2,     8,     9,    14,    14,    11,    11,     4,     4,     1,     1,    -1],
      [    0,     2,     2,     1,     3,     8,     7,    12,    15,    10,    10,     5,     3,     2,     2,     0],
      [    1,     4,     3,     3,     5,     6,     5,    13,    15,     8,     8,     6,     3,     1,     3,     1],
      [    4,     5,     5,     6,     6,     4,     7,    11,    14,     9,     7,     6,     5,     3,     4,     2],
      [    9,     9,     8,     8,     7,     7,     6,    12,    14,     9,     7,     4,     6,     5,     7,     6],
      [   10,    10,    11,    11,    12,    12,    13,    10,    13,    13,     9,    10,     8,     9,     7,     8],
      [   15,    14,    15,    15,    14,    14,    13,    11,    11,    10,    12,    11,    13,    12,    14,    13],
      [   13,    14,    12,    13,    11,    12,    10,    11,    11,    13,    14,    14,    15,    15,    14,    15],
      [    8,     7,     9,     8,    10,     9,    13,    13,    10,    13,    12,    12,    11,    11,    10,    10],
      [    6,     7,     5,     6,     4,     7,     9,    14,    12,     6,     7,     7,     8,     8,     9,     9],
      [    2,     4,     3,     5,     6,     7,     9,    14,    11,     7,     4,     6,     6,     5,     5,     4],
      [    1,     3,     1,     3,     6,     8,     8,    15,    13,     5,     6,     5,     3,     3,     4,     1],
      [    0,     2,     2,     3,     5,    10,    10,    15,    12,     7,     8,     3,     1,     2,     2,     0],
      [   -1,     1,     1,     4,     4,    11,    11,    14,    14,     9,     8,     2,     3,     0,     1,    -1],
      [   -1,    -1,     0,     2,     5,    12,    12,    15,    13,     9,    10,     4,     2,     1,    -1,    -1]
    ]

    self.labels: typing.List[typing.List[QLabel]] = []
    layout = QGridLayout()

    font = QFont('Monospace')
    #font.setStyleHint(QFont.StyleHint.TypeWriter)

    for row in range(16):
      self.labels.append([])
      for column in range(16):
        label = QLabel(self.nidaq[row][column] + ' ' + chr(65 + row) + str(column))
        label.setFont(font)
        layout.addWidget(label, row, column)
        self.labels[-1].append(label)

    self.setLayout(layout)
    self.mux = mux
    self.set_mux(mux)

  def set_mux(self, mux):
    self.mux = mux
    for row in range(16):
      for column in range(16):
        cell_mux = self.mux_map[row][column]
        label = self.labels[row][column]

        if self.mux == cell_mux:
          label.setStyleSheet('QLabel { background-color: green; }')
        elif cell_mux == -1:
          label.setStyleSheet('QLabel { background-color: black; }')
        else:
          label.setStyleSheet('QLabel { background-color: white; }')

class MicroprobeWidget(QWidget):
  def __init__(self, mux):
    super().__init__()

    self.nidaq = [
        ['AO2 12',    '',    '',    ''],
        ['AO3 13', 'AO2 11', 'AO2 10', 'AO2  9'],
        [   '',    '', 'AO0  7', 'AO2  8'],
        ['AO3  2', 'AO0  5', 'AO1  4', 'AO0  6'],
        ['AO1  3',    '',    '',    '']
    ]
    self.mux_map = [
        [ 0,  -1, -1, -1],
        [ 8,   1,  2,  3],
        [-1,  -1,  2,  4],
        [ 0,   0,  1,  1],
        [15,  -1, -1, -1]
    ]

    self.labels: typing.List[typing.List[QLabel]] = []
    layout = QGridLayout()

    font = QFont('Monospace')
    #font.setStyleHint(QFont.StyleHint.TypeWriter)

    for row in range(5):
      self.labels.append([])
      for column in range(4):
        label = QLabel(self.nidaq[row][column])
        label.setFont(font)
        layout.addWidget(label, row, column)
        self.labels[-1].append(label)

    self.setLayout(layout)
    self.mux = mux
    self.set_mux(mux)

  def set_mux(self, mux):
    self.mux = mux
    for row in range(5):
      for column in range(4):
        cell_mux = self.mux_map[row][column]
        label = self.labels[row][column]

        if self.mux == cell_mux:
          label.setStyleSheet('QLabel { background-color: green; }')
        elif cell_mux == -1:
          label.setStyleSheet('QLabel { background-color: black; }')
        else:
          label.setStyleSheet('QLabel { background-color: white; }')

def create_widget(task_config: ObservableCollection) -> QWidget:
  """
  Creates a widget for configuring the simple task
  """
  result = QWidget()
  layout = QVBoxLayout()
  result.setLayout(layout)

  """
  Below: We're building a Form (widgets.py) object that will use task_config to initialize
  the parameters of this task. Values are taken from the provided "task_config" argument, and
  if the key (e.g. intertrial_timeout) is not found in the task_config, the parameters will
  default to the values provided below. The build function also wires up all the
  listeners to update the task_config when changes are made.
  """
  form = Form.build(task_config, ["Name:", "Min:", "Max:"],
    Form.Uniform('Intertrial Interval', 'intertrial_timeout', 1, 1, 's'),
    Form.Uniform('Success Interval', 'success_timeout', 1, 1, 's'),
    Form.Uniform('Fail Interval', 'fail_timeout', 1, 1, 's'),
    Form.Uniform('Start Interval', 'start_timeout', 1, 1, 's'),
    Form.Uniform('Hold Interval', 'hold_timeout', 1, 1, 's'),
    Form.Uniform('Blink Interval', 'blink_timeout', 1, 1, 's'),
    Form.Uniform('Baseline Interval', 'baseline_timeout', 1, 1, 's'),
    Form.Constant('Window Size', 'window_size', 0, '\u00B0'),
    Form.Color('Color', 'target_color', QColor(255, 255, 255)),
    Form.Constant('Mux', 'Mux', 0,),
    Form.Choice('Electrode', 'Electrode', [
      ('Microprobe', 'Microprobe'),
      ('uECog-244ch-V5', 'uECog-244ch-V5')
    ])
  )
  layout.addWidget(form)
  stim_widget = StimWidget(task_config)
  layout.addWidget(stim_widget)

  mux_spinbox = form.findChild(QDoubleSpinBox, 'Mux')
  assert mux_spinbox is not None
  mux_spinbox.setMaximum(15)
  electrode_widget: QWidget = MicroprobeWidget(0)
  layout.addWidget(electrode_widget)

  def on_change(source, action, key, value):
    nonlocal electrode_widget

    if key == 'Electrode':
      if electrode_widget:
        layout.removeWidget(electrode_widget)
        electrode_widget.setParent(None)
        electrode_widget.deleteLater()

      if value == 'Microprobe':
        electrode_widget = MicroprobeWidget(task_config['Mux'])
      else:
        electrode_widget = UECog244ChV5Widget(task_config['Mux'])
      layout.addWidget(electrode_widget)
    elif key == 'Mux':
      electrode_widget.set_mux(round(value))

  task_config.add_recursive_observer(on_change, lambda: isdeleted(result), True)

  return result

class State(enum.Enum):
  NONE = enum.auto()
  FAIL = enum.auto()
  SUCCESS = enum.auto()
  INTERTRIAL = enum.auto()
  START_ON = enum.auto()
  START_ACQ = enum.auto()
  GO = enum.auto()
  TARGS_ACQ = enum.auto()

def ecc_to_px(ecc, dpi):
  """
  converts degrees of eccentricity to pixels relative to the optical center.
  """
  d_m = 0.4 # meters (approximate, TODO: get proper measurement)
  x_m = d_m*numpy.tan(numpy.radians(ecc))
  x_inch = x_m/0.0254
  x_px = x_inch*dpi
  return x_px

IS_SETUP = False

async def run(context: TaskContextProtocol) -> TaskResult: #pylint: disable=too-many-statements
  """
  Implementation of the state machine for the simple task
  """
  global IS_SETUP
  assert context.widget, 'Widget is None'

  intertrial_timeout = datetime.timedelta(seconds=context.get_value('intertrial_timeout'))
  baseline_timeout = datetime.timedelta(seconds=context.get_value('baseline_timeout'))
  blink_timeout = datetime.timedelta(seconds=context.get_value('blink_timeout'))
  success_timeout = datetime.timedelta(seconds=context.get_value('success_timeout'))
  fail_timeout = datetime.timedelta(seconds=context.get_value('fail_timeout'))
  start_timeout = datetime.timedelta(seconds=context.get_value('start_timeout'))
  window_size = context.get_value('window_size')

  dpi = context.config.get('dpi', None) or context.widget.logicalDpiX()
  window_size = ecc_to_px(window_size, dpi)
  window_size_squared = window_size*window_size

  stimulation_channel = context.task_config['Stimulation Channel']
  all_enabled = [False, False, False, False]

  print('stimulation_channel', stimulation_channel)
  if stimulation_channel == "AO3":
    all_enabled[3] = True
    ao = 3
  elif stimulation_channel == "AO2":
    all_enabled[2] = True
    ao = 2
  elif stimulation_channel == "AO1":
    all_enabled[1] = True
    ao = 1
  elif stimulation_channel == "AO0":
    all_enabled[0] = True
    ao = 0
    
  mux = round(context.task_config['Mux'])
  mux_bits = [5*((mux >> i) & 1) for i in range(4)]
  stim_bits = [5 if e else 0 for e in all_enabled]
  stim_bits = [stim_bits[1], stim_bits[3], stim_bits[2], stim_bits[0]]
  
  #stim_request = StimRequest()
  stim_declaration: StimDeclaration = StimDeclaration()
  stim_data: AnalogResponse = stim_declaration.data
  stim_data.channel_type = AnalogResponse.ChannelType.Voltage
  #waveform = compute_waveform(context.task_config)
  #if waveform is not None:
  #  stim_data.data.extend(waveform)
  #  stim_data.spans.append(Span(begin=0,end=len(stim_data.data),name=f'/PXI1Slot4/ao{ao}'))
  #  stim_data.sample_intervals.append(int(1e9/125e3))

  #mux_signal = AnalogResponse(
  #  data=mux_bits + stim_bits + [5],
  #  spans=[
  #    Span(begin=0,end=1,name='/PXI1Slot4/port0/line20'),
  #    Span(begin=1,end=2,name='/PXI1Slot4/port0/line29'),
  #    Span(begin=2,end=3,name='/PXI1Slot4/port0/line19'),
  #    Span(begin=3,end=4,name='/PXI1Slot4/port0/line26'),
  #    Span(begin=4,end=5,name='/PXI1Slot4/port0/line7'),
  #    Span(begin=5,end=6,name='/PXI1Slot4/port0/line2'),
  #    Span(begin=6,end=7,name='/PXI1Slot4/port0/line1'),
  #    Span(begin=7,end=8,name='/PXI1Slot4/port0/line6'),
  #    Span(begin=8,end=9,name='/PXI1Slot4/port0/line8'),
  #  ],
  #  sample_intervals=[0, 0, 0, 0, 0, 0, 0, 0, 0])
  #print(mux_signal)
  #await context.inject_analog('Mux', mux_signal)

  if IS_SETUP:
    await context.node_request('Node 1', {
      'type': 'teardown',
    })
    IS_SETUP = False

  await context.node_request('Node 1', {
    'type': 'setup',
    "amp_uA": context.task_config['Amplitude (uA)'],
    "pw_us": context.task_config['Pulse Width (us)'],
    "freq_hz": context.task_config['Frequency (Hz)'],
    "ipd_ms": context.task_config['Interphase Delay (ms)'],
    "num_pulses": context.task_config['Number of Pulses'],
    "stim_dur_s": context.task_config['Stimulation Duration (s)'],
    "is_biphasic": context.task_config['Phase'] == 'Biphasic',
    "polarity": 1 if context.task_config['Lead'] == 'Cathode-leading' else -1,
    "dis_dur_s": context.task_config['Discharge Duration (s)']
  })
  IS_SETUP = True

  display_indicator = False
  state = State.NONE
  touched = False
  async def transition(new_state: State, toggle_display=True):
    nonlocal state, display_indicator, touched
    state = new_state
    touched = False
    if toggle_display:
      display_indicator = not display_indicator
      context.widget.update()
    await context.log(f'BehavState={state.name}')

  async def fail():
    await transition(State.FAIL, False)
    await context.sleep(fail_timeout)
    return TaskResult(False)

  fixation_point = QPoint(int(context.widget.width()/2), int(context.widget.height()/2))
  fixating = False
  def gaze_handler(cursor: QPoint) -> None:
    nonlocal fixating
    offset = cursor - fixation_point
    fixating = QPoint.dotProduct(offset, offset) < window_size_squared

  def touch_handler(cursor: QPoint):
    nonlocal touched
    if cursor.x() < 0:
      return
    touched = True

  deliver_stim = False
  def renderer(painter: QPainter):
    nonlocal display_indicator, deliver_stim
    if state == State.START_ON:
      rect = QRect(fixation_point.x() - 20, fixation_point.y() - 20, 40, 40)
      painter.fillRect(rect, Qt.GlobalColor.red)
    elif state == State.SUCCESS:
      if deliver_stim and painter.output_mask == RenderOutput.SUBJECT:
        print('STIMMING')

        create_task_with_exc_handling(asyncio.gather(
          context.node_request('Node 1', {
            'type': 'stim',
          }),
          context.log('STIM')
        ))
        deliver_stim = False
        
    if display_indicator:
      indicator_size = 150
      indicator_rect = QRect(
        context.widget.width()-indicator_size,
        context.widget.height()-indicator_size,
        indicator_size, indicator_size)
      painter.fillRect(indicator_rect, Qt.GlobalColor.white)

    with painter.masked(RenderOutput.OPERATOR):
      painter.setBrush(QColor(255, 255, 255, 128))
      painter.drawEllipse(QPointF(fixation_point), window_size, window_size)

  context.widget.touch_listener = touch_handler
  context.widget.gaze_listener = gaze_handler
  context.widget.renderer = renderer

  while True:
    await transition(State.INTERTRIAL)
    await wait_for(context, lambda: touched, intertrial_timeout)
    if touched:
      return await fail()

    await transition(State.START_ON)
    await wait_for(context, lambda: touched or fixating, start_timeout)
    if touched:
      return await fail()

    if fixating:
      break

  await wait_for_hold(context, lambda: fixating, baseline_timeout, blink_timeout)

  deliver_stim = True
  await transition(State.SUCCESS)
  await context.sleep(success_timeout)

  return TaskResult(True)
