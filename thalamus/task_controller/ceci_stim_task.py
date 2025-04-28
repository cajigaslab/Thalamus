"""
Implementation of the simple task
"""
import typing
import logging
import datetime
import typing_extensions
import asyncio

import numpy

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
    Form.Constant('Window Size', 'window_size', 0, '\u00B0'),
    Form.Color('Color', 'target_color', QColor(255, 255, 255)),
    Form.Bool('AO0 Enabled', 'AO0 Enabled', False),
    Form.Constant('AO0 Amplitude (uA)', 'AO0 Amplitude', 1),
    Form.Constant('AO0 Frequency', 'AO0 Frequency', 1),
    Form.Constant('AO0 Count', 'AO0 Count', 1),
    Form.Bool('AO1 Enabled', 'AO1 Enabled', False),
    Form.Constant('AO1 Amplitude (uA)', 'AO1 Amplitude', 1),
    Form.Constant('AO1 Frequency', 'AO1 Frequency', 1),
    Form.Constant('AO1 Count', 'AO1 Count', 1),
    Form.Bool('AO2 Enabled', 'AO2 Enabled', False),
    Form.Constant('AO2 Amplitude (uA)', 'AO2 Amplitude', 1),
    Form.Constant('AO2 Frequency', 'AO2 Frequency', 1),
    Form.Constant('AO2 Count', 'AO2 Count', 1),
    Form.Bool('AO3 Enabled', 'AO3 Enabled', False),
    Form.Constant('AO3 Amplitude (uA)', 'AO3 Amplitude', 1),
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

  intertrial_timeout = datetime.timedelta(seconds=context.get_value('intertrial_timeout'))
  success_timeout = datetime.timedelta(seconds=context.get_value('success_timeout'))
  fail_timeout = datetime.timedelta(seconds=context.get_value('fail_timeout'))
  start_timeout = datetime.timedelta(seconds=context.get_value('start_timeout'))
  window_size = context.get_value('window_size')

  dpi = context.config.get('dpi', None) or context.widget.logicalDpiX()
  window_size = ecc_to_px(window_size, dpi)
  window_size_squared = window_size*window_size

  volt_per_amp = 10e3
  amp_per_uamp = 1e-6
  volt_per_uamp = amp_per_uamp*volt_per_amp

  sample_rate = 10e3
  sample_interval = int(1e9/sample_rate)
  all_enabled = [context.task_config['AO0 Enabled'], context.task_config['AO1 Enabled'], context.task_config['AO2 Enabled'], context.task_config['AO3 Enabled']]
  all_count = [context.task_config['AO0 Count'], context.task_config['AO1 Count'], context.task_config['AO2 Count'], context.task_config['AO3 Count']]
  all_frequency = [context.task_config['AO0 Frequency'], context.task_config['AO1 Frequency'], context.task_config['AO2 Frequency'], context.task_config['AO3 Frequency']]
  all_amplitude = numpy.array([
    context.task_config['AO0 Amplitude'],
    context.task_config['AO1 Amplitude'],
    context.task_config['AO2 Amplitude'],
    context.task_config['AO3 Amplitude']])*volt_per_uamp
  print(all_amplitude)
    
  mux = round(context.task_config['Mux'])
  mux_bits = [5*((mux >> i) & 1) for i in range(4)]
  stim_bits = [5 if e else 0 for e in all_enabled]
  stim_bits = [stim_bits[1], stim_bits[3], stim_bits[2], stim_bits[0]]

  max_duration = max(a/b for a,b in zip(all_count, all_frequency))
  max_duration = min(max_duration, 1)
  max_samples = int(max_duration*sample_rate)
  
  #stim_request = StimRequest()
  stim_declaration: StimDeclaration = StimDeclaration()
  stim_data: AnalogResponse = stim_declaration.data
  stim_data.channel_type = AnalogResponse.ChannelType.Voltage
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
  

  mux_signal = AnalogResponse(
    data=mux_bits + stim_bits + [5],
    spans=[
      Span(begin=0,end=1,name='/PXI1Slot4/port0/line20'),
      Span(begin=1,end=2,name='/PXI1Slot4/port0/line29'),
      Span(begin=2,end=3,name='/PXI1Slot4/port0/line19'),
      Span(begin=3,end=4,name='/PXI1Slot4/port0/line26'),
      Span(begin=4,end=5,name='/PXI1Slot4/port0/line7'),
      Span(begin=5,end=6,name='/PXI1Slot4/port0/line2'),
      Span(begin=6,end=7,name='/PXI1Slot4/port0/line1'),
      Span(begin=7,end=8,name='/PXI1Slot4/port0/line6'),
      Span(begin=8,end=9,name='/PXI1Slot4/port0/line8'),
    ],
    sample_intervals=[0, 0, 0, 0, 0, 0, 0, 0, 0])
  print(mux_signal)
  await context.inject_analog('Mux', mux_signal)
  await context.arm_stim('Ceci', stim_declaration)

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
          context.log('STIM'),
          context.trigger_stim('Ceci')
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
      painter.drawEllipse(fixation_point, window_size, window_size)

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

  deliver_stim = True
  await transition(State.SUCCESS)
  await context.sleep(success_timeout)

  return TaskResult(True)
