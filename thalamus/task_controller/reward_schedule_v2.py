"""
Defines the RewardSchedule widget
"""

import typing
import functools

from ..qt import *

import matplotlib
from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg # type: ignore
from matplotlib.figure import Figure # type: ignore
from matplotlib import pyplot
from .widgets import Form, ListAsTabsWidget

from ..config import ObservableCollection

matplotlib.use('Qt5Agg')

COLORS = [
  QColor(int(c[0]*255), int(c[1]*255), int(c[2]*255)) for c in pyplot.get_cmap('tab10').colors
]

class RewardSchedule(QWidget): # type: ignore
  '''
  Displays the reward schedule
  '''
  def __init__(self, config: ObservableCollection):
    super().__init__()
    self.config = config

    if 'schedules' not in config:
      config['schedules'] = []

    self.paths = len(config['schedules'])*[None]
    self.ranges = len(config['schedules'])*[(float('inf'), float('-inf'))]
    self.range = float('inf'), float('-inf')
    self.need_graph_regen = True

    config.add_observer(self.on_schedule_change, lambda: isdeleted(self))
    self.on_schedule_change(ObservableCollection.Action.SET, None, None)

  def sizeHint(self) -> QSize:
    return QSize(200, 200)

  def regen_graphs(self):
    print('regen_graphs')
    for k, graph in enumerate(self.config['schedules']):
      path = QPainterPath()
      enumeration = enumerate(graph)
      i, y = next(enumeration)
      path.moveTo(float(i), float(y))
      for i, y in enumeration:
        path.lineTo(float(i), float(y))
      if len(self.paths) <= k:
        self.paths.insert(k, path)
        self.ranges.insert(k, (min(graph), max(graph)))
      else:
        self.paths[k] = path
        self.ranges[k] = (min(graph), max(graph))

      self.range = functools.reduce(lambda a, b: (min(a[0], b[0]), max(a[1], b[1])), self.ranges, (float('inf'), float('-inf')))
      self.length = max([p.elementCount() for p in self.paths if p is not None] + [0])

  def paintEvent(self, event: QPaintEvent):
    print('update')
    super().paintEvent(event)
    if self.need_graph_regen:
      self.regen_graphs()
      self.need_graph_regen = False

    if self.range[0] > 1e200 or self.range[1] < -1e200 or self.range[0] == self.range[1] or self.length == 0:
      return
    painter = QPainter(self)

    metrics = QFontMetrics(painter.font())
    
    pen = painter.pen()
    pen.setCosmetic(True)
    vertical_scale = (self.height() - 2*metrics.height())/(self.range[1] - self.range[0])
    painter.translate(metrics.height(), self.height() + self.range[0]*vertical_scale - metrics.height())
    painter.scale((self.width() - 2*metrics.height())/self.length, -vertical_scale)
    for path, color in zip(self.paths, COLORS):
      pen.setColor(color)
      painter.setPen(pen)
      painter.drawPath(path)
    pen.setColor(Qt.GlobalColor.red)
    painter.setPen(pen)
    painter.drawLine(QLineF(self.config['index'], self.range[0], self.config['index'], self.range[1]))
    
    pen.setColor(Qt.GlobalColor.black)
    painter.setPen(pen)
    painter.drawRect(QRectF(0, self.range[0], self.length, self.range[1] - self.range[0]))

    painter.resetTransform()

    painter.drawText(0, metrics.height(), str(self.range[1]))
    painter.drawText(0, self.height(), str(self.range[0]))






  def on_schedule_change(self, _a: ObservableCollection.Action, k: typing.Any, _v: typing.Any) -> None:
    '''
    Updates the UI as the reward schedule changes
    '''
    if k != 'index':
      self.need_graph_regen = True
    self.update()


  def create_widget(self,config_data: ObservableCollection) -> QWidget:
    """
    Creates a widget for configuring the reward
    """
    result = QWidget()
    layout = QVBoxLayout()
    result.setLayout(layout)

    """
    Below: We're building a Form (widgets.py) object that will use task_config to initialize
    the parameters of the reward schedule. Values are taken from the provided "task_config" argument, and
    if the key (e.g. reward_channel_0) is not found in the task_config, the parameters will
    default to the values provided below. The build function also wires up all the
    listeners to update the task_config when changes are made.
    """
    form = Form.build(config_data, ["Name:", "Min:", "Max:"],
      Form.Bool('Use schedule from config/file', 'use_reward_config', True),
      Form.Uniform('Reward channel 0', 'reward_channel_0', 0, 0, 'ms'),
      Form.Uniform('Reward channel 1', 'reward_channel_1', 100, 100, 'ms'),
      Form.Uniform('Reward channel 2', 'reward_channel_2', 200, 200, 'ms'),
      Form.Uniform('Reward channel 3', 'reward_channel_3', 250, 250, 'ms'),
      Form.Uniform('Reward channel 4', 'reward_channel_4', 300, 300, 'ms'),
      Form.Uniform('Reward channel 5', 'reward_channel_5', 350, 350, 'ms'),
      Form.Uniform('Reward channel 6', 'reward_channel_6', 400, 400, 'ms'),
      Form.Uniform('Reward channel 7', 'reward_channel_7', 450, 450, 'ms'),
    )
    layout.addWidget(form)
    plot = RewardSchedule(self.task_context.config_data['reward_schedule'])
    layout.addWidget(plot)
