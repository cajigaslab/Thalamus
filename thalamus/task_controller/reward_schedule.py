"""
Defines the RewardSchedule widget
"""

import typing
import functools

from PyQt5 import QtCore

from ..qt import *

import matplotlib
from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg # type: ignore
from matplotlib.figure import Figure # type: ignore

from ..config import ObservableCollection

matplotlib.use('Qt5Agg')

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

    config['schedules'].add_observer(self.on_graph_change, lambda: isdeleted(self))
    for i, g in enumerate(config['schedules']):
      self.on_graph_change(ObservableCollection.Action.SET, i, g)

    config.add_observer(self.on_schedule_change, lambda: isdeleted(self))
    self.on_schedule_change(ObservableCollection.Action.SET, None, None)

  def sizeHint(self) -> QSize:
    return QSize(200, 200)

  def on_graph_change(self, _a: ObservableCollection.Action, k: typing.Any, graph: typing.Any) -> None:
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
    super().paintEvent(event)
    if self.range[0] > 1e200 or self.range[1] < -1e200 or self.range[0] == self.range[1] or self.length == 0:
      return
    painter = QPainter(self)

    metrics = QFontMetrics(painter.font())
    
    pen = painter.pen()
    pen.setCosmetic(True)
    vertical_scale = (self.height() - 2*metrics.height())/(self.range[1] - self.range[0])
    painter.translate(metrics.height(), self.height() + self.range[0]*vertical_scale - metrics.height())
    painter.scale((self.width() - 2*metrics.height())/self.length, -vertical_scale)
    for path, color in zip(self.paths, [Qt.GlobalColor.blue, Qt.GlobalColor.red, Qt.GlobalColor.green]):
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

  def on_schedule_change(self, _a: ObservableCollection.Action, _k: typing.Any, _v: typing.Any) -> None:
    '''
    Updates the UI as the reward schedule changes
    '''
    self.update()
