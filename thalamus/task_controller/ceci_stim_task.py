"""
Implementation of the simple task
"""
import typing
import logging
import datetime
import typing_extensions

import numpy

from ..qt import *

from . import task_context
from .widgets import Form, ListAsTabsWidget
from .util import wait_for, wait_for_hold, TaskResult, TaskContextProtocol, CanvasPainterProtocol
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
    Form.Uniform('Start Interval', 'start_timeout', 1, 1, 's'),
    Form.Uniform('Hold Interval', 'hold_timeout', 1, 1, 's'),
    Form.Uniform('Blink Interval', 'blink_timeout', 1, 1, 's'),
    Form.Constant('Window Size', 'window_size', 0, '\u00B0'),
    Form.Color('Color', 'target_color', QColor(255, 255, 255)),
    Form.Bool('AO0 Enabled', 'AO0 Enabled', False),
    Form.Constant('AO0 Amplitude', 'AO0 Amplitude', 1),
    Form.Constant('AO0 Frequency', 'AO0 Frequency', 1),
    Form.Constant('AO0 Count', 'AO0 Count', 1),
    Form.Bool('AO1 Enabled', 'AO1 Enabled', False),
    Form.Constant('AO1 Amplitude', 'AO1 Amplitude', 1),
    Form.Constant('AO1 Frequency', 'AO1 Frequency', 1),
    Form.Constant('AO1 Count', 'AO1 Count', 1),
    Form.Bool('AO2 Enabled', 'AO2 Enabled', False),
    Form.Constant('AO2 Amplitude', 'AO2 Amplitude', 1),
    Form.Constant('AO2 Frequency', 'AO2 Frequency', 1),
    Form.Constant('AO2 Count', 'AO2 Count', 1),
    Form.Bool('AO3 Enabled', 'AO3 Enabled', False),
    Form.Constant('AO3 Amplitude', 'AO3 Amplitude', 1),
    Form.Constant('AO3 Frequency', 'AO3 Frequency', 1),
    Form.Constant('AO3 Count', 'AO3 Count', 1),
    Form.Constant('Mux', 'Mux', 0,),
    Form.Choice('Electrode', 'Electrode', [
      ('Microprobe', 'Microprobe'),
      ('uECog-244ch-V5', 'uECog-244ch-V5')
    ])
  )
  layout.addWidget(form)

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

async def run(context: TaskContextProtocol) -> TaskResult: #pylint: disable=too-many-statements
  """
  Implementation of the state machine for the simple task
  """

  """
  Below is an object that contains a realization generated by sampling from the random
  distributions defined in the task_config. It itself has no logic, it simply holds
  the realization's values.
  """
  assert context.widget, 'Widget is None'

  sample_rate = 10e3
  sample_interval = int(1e9/sample_rate)
  all_enabled = [context.task_config['AO0 Enabled'], context.task_config['AO1 Enabled'], context.task_config['AO2 Enabled'], context.task_config['AO3 Enabled']]
  all_count = [context.task_config['AO0 Count'], context.task_config['AO1 Count'], context.task_config['AO2 Count'], context.task_config['AO3 Count']]
  all_frequency = [context.task_config['AO0 Frequency'], context.task_config['AO1 Frequency'], context.task_config['AO2 Frequency'], context.task_config['AO3 Frequency']]
  all_amplitude = [context.task_config['AO0 Amplitude'], context.task_config['AO1 Amplitude'], context.task_config['AO2 Amplitude'], context.task_config['AO3 Amplitude']]

  max_duration = max(a/b for a,b in zip(all_count, all_frequency))
  max_duration = min(max_duration, 1)
  max_samples = int(max_duration*sample_rate)
  
  #stim_request = StimRequest()
  stim_declaration: StimDeclaration = StimDeclaration()
  stim_data: AnalogResponse = stim_declaration.data
  for i, values in enumerate(zip(all_enabled, all_count, all_frequency, all_amplitude)):
    enabled, count, frequency, amplitude = values
    span_start = len(stim_data.data)
    if enabled:
      period = 1/frequency
      period_samples = int(period*sample_rate)
      period_samples_half = period_samples/2
      signal_samples = int(period_samples*count)
      signal_samples = min(signal_samples, max_samples)
      channel_data = numpy.zeros((max_samples,))
      channel_data[:signal_samples] = amplitude*(1 - ((numpy.arange(signal_samples)//period_samples_half) % 2))
      channel_data[-1] = 0
      stim_data.data.extend(channel_data)
    else:
      stim_data.data.extend(numpy.zeros((max_samples,)))
    stim_data.spans.append(Span(begin=span_start,end=len(stim_data.data),name=f'/PXI1Slot4/ao{i}'))
    stim_data.sample_intervals.append(sample_interval)

  await context.arm_stim('Ceci', stim_declaration)

  await context.sleep(datetime.timedelta(seconds=1))
  await context.trigger_stim('Ceci')
  await context.sleep(datetime.timedelta(seconds=1))

  return TaskResult(True)

  config = Config(
    datetime.timedelta(seconds=context.get_value('intertrial_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('start_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('hold_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('blink_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('fail_timeout', RANDOM_DEFAULT)),
    datetime.timedelta(seconds=context.get_value('success_timeout', RANDOM_DEFAULT)),
    QRect(int(context.get_value('target_x', RANDOM_DEFAULT)),
                       int(context.get_value('target_y', RANDOM_DEFAULT)),
                       int(context.get_value('target_width', RANDOM_DEFAULT)),
                       int(context.get_value('target_height', RANDOM_DEFAULT))),
    context.get_color('target_color', COLOR_DEFAULT)
  )

  """
  Defining drawing and cursor behavior.
  """

  target_acquired = False
  def touch_handler(cursor: QPoint) -> None:
    nonlocal target_acquired
    target_acquired = config.target_rectangle.contains(cursor)

  context.widget.touch_listener = touch_handler

  show_target = False
  def renderer(painter: CanvasPainterProtocol) -> None:
    if show_target:
      painter.fillRect(config.target_rectangle, config.target_color)

  context.widget.renderer = renderer

  while True:
    await context.log('BehavState=intertrial')
    show_target = False
    context.widget.update()
    await context.sleep(config.intertrial_timeout)

    await context.log('BehavState=start_on')
    show_target = True
    context.widget.update()
    acquired = await wait_for(context, lambda: target_acquired, config.start_timeout)

    if acquired:
      break

  # state: startacq
  success = await wait_for_hold(context, lambda: target_acquired, config.hold_timeout, config.blink_timeout)

  """
  The trial's outcome (success or failure) at this point is decided, and now
  we can wait (optionally) by success_timeout or fail_timeout.
  """
  show_target = False
  context.widget.update()
  if success:
    await context.log('BehavState=success')

    await context.sleep(config.success_timeout)
    return TaskResult(True)

  await context.log('BehavState=fail')

  await context.sleep(config.fail_timeout)
  return TaskResult(False)
  #pylint: disable=unreachable
  """
  The return value is a TaskResult instance, and this contains the success/failure,
  as well as maybe other things that we want to add.
  """
