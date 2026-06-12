import re
import sys
import asyncio
import typing
import json
import bisect
import time
import datetime
import copy
import dataclasses

import grpc

import numpy
import scipy.spatial.transform
import scipy.optimize

from thalamus.qt import *
from thalamus import thalamus_pb2
from thalamus import thalamus_pb2_grpc
from thalamus.task_controller.util import RenderOutput

from thalamus.util import IterableQueue, MeteredUpdater
from thalamus.config import ObservableDict

from thalamus.thread import ThalamusThread
print(thalamus_pb2.__file__)

POINT_SIZE = 10
QUAD_TO_LABEL = {
  (True, True): 'IV',
  (True, False): 'II',
  (False, True): 'III',
  (False, False): 'I',
}

DEFAULT_PROJECTIVE = {
  'Parameters': [0.5, 0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.0],
  'Distance (m)': 1.0,
  'DPI': 100.0,
}

DEFAULT_ANGULAR_SCALING = {
  'Pins': [],
  'Scale Default': 100.0
}

@dataclasses.dataclass
class SaccadeTarget:
  x: int
  y: int
  pin: int
  hidden: bool

class Task:
  def __init__(self, stub: thalamus_pb2_grpc.ThalamusStub, config: dict):
    self.stub = stub
    self.oculomatic_task: asyncio.Task | None = None
    self.task: asyncio.Task | None = None
    self.show_fixation = False
    self.show_saccade = False
    self.fixation_radius = 50
    self.saccade_radius = 100
    self.current_saccade = 0
    self.reward_ms = 500
    self.saccades: list[SaccadeTarget] = []
    self.path = QPainterPath()
    self.training_path = QPainterPath()
    self.points = []
    self.reward_queue = IterableQueue()
    self.distance_m = 1.0
    self.dpi = 1.0
    self.model_name = 'Projective'
    self.config = config
    self.training_data = []
    self.nudge_index = -1
    self.nudge_start_value = None
    self.mouse_pos = None
    self.mouse_pos_scaled = None
    self.seen_points = set()
    self.hold = False
    self.eye_opacity = 192
    self.grid = QPainterPath()

    eye_scaling = config['eye_scaling']
    if 'Reward Node' not in eye_scaling:
      eye_scaling['Reward Node'] = 'reward_in'
    models = None
    projective = None
    self.angular = None
    self.pins = None
    self.pins_updater = None
    self.pin_angles = numpy.array([])
    self.pin_notches = []

    def on_angular_change(source, action, key, value):
      self.update_pins()

    def on_change(source, action, key, value):
      nonlocal models, projective

      if source == eye_scaling:
        if key == 'Models':
          models = value
          value.recap()
        elif key == 'Selected Model':
          self.model_name = value
          self.rebuild()
        elif key == 'Reward Node':
          asyncio.create_task(self.reward_queue.put(thalamus_pb2.InjectAnalogRequest(node=value)))
      elif source is models:
        if key == 'Projective':
          projective = value
          value.recap()
        elif key == 'Angular Scaling':
          self.angular = value
          self.angular.add_recursive_observer(on_angular_change)
          value.recap()
      elif source is projective:
        if key == 'Distance (m)':
          self.distance_m = value
          self.rebuild()
        elif key == 'DPI':
          self.dpi = value
          self.rebuild()
      elif source is self.angular:
        if key == 'Pins':
          self.pins = value
          self.pins_updater = MeteredUpdater(value, datetime.timedelta(seconds=1), lambda: False)

      #print(key, value)
      #print('params', self.distance_m, self.dpi)

    eye_scaling.add_recursive_observer(on_change)
    eye_scaling.recap()

  def update_pins(self):
    if 'Pins' not in self.angular:
      return
    
    pins = self.angular['Pins']
    if len(pins) != 0 and not isinstance(pins[0], ObservableDict):
      return
    
    self.pin_angles = []
    self.pin_notches = []

    for pin in pins:

      self.pin_angles.append([pin['Angle'], pin['Rotation']])
      self.pin_notches.append([[0.0, 0.0]])
      for notch in pin['Notches']:
        self.pin_notches[-1].append([notch["Eye"], notch["Screen"]])

    self.pin_angles = numpy.array(self.pin_angles)
    self.pin_notches = [numpy.array(n) for n in self.pin_notches]

  async def prepare_reward(self):
    self.stub.inject_analog(self.reward_queue)

  async def deliver_reward(self):
    await self.reward_queue.put(thalamus_pb2.InjectAnalogRequest(signal=thalamus_pb2.AnalogResponse(
      data=[5,0],
      spans=[thalamus_pb2.Span(begin=0,end=2,name='Reward')],
      sample_intervals=[1_000_000*self.reward_ms])))

  def render(self, painter: QPainter, size: QSize, output: RenderOutput):
    painter.setBrush(Qt.GlobalColor.black)
    painter.drawRect(0, 0, size.width(), size.height())

    radius = max(size.width(), size.height())
    
    fixation_color = QColor(Qt.GlobalColor.red)
    fixation_shape = QRect(size.width()//2 - self.fixation_radius, size.height()//2 - self.fixation_radius,
                           2*self.fixation_radius, 2*self.fixation_radius)
    
    saccade_color = QColor(Qt.GlobalColor.white)
    saccade_shape = QRect(0, 0,
                           2*self.saccade_radius, 2*self.saccade_radius)
    if output == RenderOutput.OPERATOR:
      painter.save()
      painter.setPen(Qt.GlobalColor.yellow)
      painter.drawLine(0, size.height()//2, size.width(), size.height()//2)
      painter.drawLine(size.width()//2, 0, size.width()//2, size.height())
      painter.drawLine(0, 0, size.width(), size.height())
      painter.drawLine(0, size.height(), size.width(), 0)
      painter.drawArc(QRect(0, 0, size.width(), size.height()), 0, 360*16)
      painter.restore()

      painter.save()
      painter.setPen(Qt.GlobalColor.magenta)
      painter.setBrush(Qt.BrushStyle.NoBrush)
      painter.translate(size.width()//2, size.height()//2)
      painter.drawPath(self.grid)
      painter.restore()

      if not self.show_fixation:
        fixation_color.setAlpha(64)

      painter.save()
      pen = painter.pen()
      pen.setColor(fixation_color)
      pen.setWidth(5)
      painter.setPen(pen)
      painter.drawLine(fixation_shape.left(), fixation_shape.center().y(), fixation_shape.right(), fixation_shape.center().y())
      painter.drawLine(fixation_shape.center().x(), fixation_shape.top(), fixation_shape.center().x(), fixation_shape.bottom())
      painter.restore()
      #painter.drawEllipse(fixation_shape)

      for i, saccade in enumerate(self.saccades):
        if self.show_saccade and i == self.current_saccade:
          saccade_color.setAlpha(255)
        else:
          saccade_color.setAlpha(64)

        painter.save()
        painter.setBrush(saccade_color)
        if i == self.current_saccade:
          pen = painter.pen()
          pen.setColor(Qt.GlobalColor.green)
          pen.setWidth(10)
          painter.setPen(pen)
        painter.drawEllipse(saccade_shape.adjusted(saccade.x - self.saccade_radius + size.width()//2,
                                                   saccade.y - self.saccade_radius + size.height()//2,
                                                   saccade.x - self.saccade_radius + size.width()//2,
                                                   saccade.y - self.saccade_radius + size.height()//2))
        painter.restore()

      #Draw all oculomatic data
      painter.save()
      pen = painter.pen()
      pen.setCosmetic(True)
      pen.setWidth(POINT_SIZE)
      pen.setColor(QColor(0, 0, 255, self.eye_opacity))
      pen.setCapStyle(Qt.PenCapStyle.RoundCap)
      painter.setPen(pen)
      painter.translate(size.width()//2, size.height()//2)

      painter.save()
      for p1, p2 in zip(self.pin_angles, self.pin_notches):
        painter.setPen(Qt.GlobalColor.green)
        angle, rotation = p1
        total_angle = angle + rotation
        x, y = radius*numpy.cos(total_angle), radius*numpy.sin(total_angle)
        painter.drawLine(0, 0, int(x), int(y))

        painter.setPen(Qt.GlobalColor.white)
        for notch in p2:
          screen_notch = int(notch[1])
          rect = QRect(-screen_notch, -screen_notch, 2*screen_notch, 2*screen_notch)
          painter.drawArc(rect, int((2*numpy.pi - total_angle)*180/numpy.pi - 5)*16, 10*16)

      painter.setPen(Qt.GlobalColor.green)
      if self.mouse_pos_scaled is not None:
        nudge_index, notch_nudge_index = self.get_start_nudge_index((self.mouse_pos_scaled[0],
                                                                     self.mouse_pos_scaled[1]))
        if nudge_index != -1:
          angle, rotation = self.pin_angles[nudge_index]
          total_angle = angle + rotation
          mag = self.pin_notches[nudge_index][notch_nudge_index+1,1]
          x = mag*numpy.cos(total_angle)
          y = mag*numpy.sin(total_angle)
          painter.drawLine(self.mouse_pos_scaled[0], self.mouse_pos_scaled[1], int(x), int(y))
      painter.restore()

      painter.drawPath(self.path)
      pen = painter.pen()
      pen.setWidth(POINT_SIZE)
      pen.setColor(Qt.GlobalColor.red)
      painter.setPen(pen)
      painter.drawPath(self.training_path)
      painter.restore()
      
    elif output == RenderOutput.SUBJECT:
      if self.show_fixation:
        painter.save()
        pen = painter.pen()
        pen.setColor(fixation_color)
        pen.setWidth(5)
        painter.setPen(pen)
        painter.drawLine(fixation_shape.left(), fixation_shape.center().y(), fixation_shape.right(), fixation_shape.center().y())
        painter.drawLine(fixation_shape.center().x(), fixation_shape.top(), fixation_shape.center().x(), fixation_shape.bottom())
        painter.restore()

      if self.saccades and self.show_saccade:
        painter.setBrush(saccade_color)
        saccade = self.saccades[self.current_saccade]
        painter.drawEllipse(saccade_shape.adjusted(saccade.x - self.saccade_radius + size.width()//2,
                                                   saccade.y - self.saccade_radius + size.height()//2,
                                                   saccade.x - self.saccade_radius + size.width()//2,
                                                   saccade.y - self.saccade_radius + size.height()//2))

  def stop(self):
    self.task.cancel()

  def start(self):
    self.task = asyncio.create_task(self.run())

  def add_target(self, x: int, y: int):
    self.saccades.append(SaccadeTarget(x, y, -1, False))
    print(self.saccades)

  def remove_target(self, x: int, y: int):
    for i, target in list(enumerate(self.saccades))[::-1]:
      distance = (target.x - x)**2 + (target.y - y)**2
      print(distance)
      if distance < self.saccade_radius**2:
        del self.saccades[i]
        if i <= self.current_saccade:
          self.current_saccade -= 1
        break

  def pin_here(self, x: int, y: int):
    target = SaccadeTarget(x, y, -1, True)
    ocu = self.pixels_from_center_to_oculomatic(x, y)

    def do():
      self.training_data.append((ocu, None, target))
      self.append_to_path(self.training_path, x, y)
      self.training_path.lineTo(x, y)

    def undo():
      self.training_data.pop()
      self.rebuild()

    return Action(do, undo)

  def get_model(self, name: str):
    models = self.config['eye_scaling']['Models']
    if name not in models:
      if name == 'Projective':
        models[name] = DEFAULT_PROJECTIVE
        return DEFAULT_PROJECTIVE
      elif name == 'Angular Scaling':
        models[name] = DEFAULT_ANGULAR_SCALING
        return DEFAULT_ANGULAR_SCALING
    return models.get(name, None)

  def oculomatic_to_pixels_from_center(self, x, y, return_ints=True):
    model = self.get_model(self.model_name)
    if self.model_name == 'Projective':
      #print(model)
      a, b, c, d, e, f, g, h = model['Parameters']
      #compute oculomatic -> angles
      tx = (a*x + b*y + c) / (g*x + h*y + 1)
      ty = (d*x + e*y + f) / (g*x + h*y + 1)

      #compute angles -> pixels
      distance_cm = 100*self.distance_m
      distance_inches = distance_cm/2.54
      px = numpy.tan(tx*numpy.pi/180)*distance_inches*self.dpi
      py = numpy.tan(ty*numpy.pi/180)*distance_inches*self.dpi
      return int(px), int(py)
    elif self.model_name == 'Angular Scaling':
      if self.pin_angles.size == 0:
        scale_default = model.get('Scale Default', 100.0)
        return int(scale_default*x), int(scale_default*y)
      
      val = numpy.arctan2(y, x)
      if val < 0:
        val = 2*numpy.pi + val

      mag = (x**2 + y**2)**.5
      if mag == 0.0:
        return 0.0, 0.0
      eye_angles = self.pin_angles[:,0]
      screen_angles = self.pin_angles[:,1]
      i = bisect.bisect_left(eye_angles, val)
      if i == len(self.pin_notches) or i == 0:
        lower_i, upper_i = -1, 0
        eye_angles_lower = eye_angles[lower_i] - 2*numpy.pi
        if val > numpy.pi:
          val -= 2*numpy.pi
      elif len(self.pin_angles) == 1:
        lower_i, upper_i = 0, 0
        eye_angles_lower = eye_angles[lower_i]
      else:
        lower_i, upper_i = i-1, i
        eye_angles_lower = eye_angles[lower_i]
      lower_notches = self.pin_notches[lower_i]
      upper_notches = self.pin_notches[upper_i]

      if mag > lower_notches[-1,0]:
        lower_scale = (mag - lower_notches[-1,0])*(lower_notches[-1,1] - lower_notches[-2,1])/(lower_notches[-1,0] - lower_notches[-2,0]) + lower_notches[-1,1]
      else:
        lower_scale = numpy.interp(mag, lower_notches[:,0], lower_notches[:,1])
      if mag > upper_notches[-1,0]:
        upper_scale = (mag - upper_notches[-1,0])*(upper_notches[-1,1] - upper_notches[-2,1])/(upper_notches[-1,0] - upper_notches[-2,0]) + upper_notches[-1,1]
      else:
        upper_scale = numpy.interp(mag, upper_notches[:,0], upper_notches[:,1])

      scale = numpy.interp(val, [eye_angles_lower, eye_angles[upper_i]], [lower_scale, upper_scale])
      rotation = numpy.interp(val, [eye_angles_lower, eye_angles[upper_i]],
                                   [screen_angles[lower_i], screen_angles[upper_i]])
      if rotation > numpy.pi:
        rotation -= 2*numpy.pi
      elif rotation < -numpy.pi:
        rotation += 2*numpy.pi

      #print(val, rotation, scale, [lower_scale, upper_scale], [eye_angles_lower, eye_angles[upper_i]])
      #print(val, lower_i)
      cos = numpy.cos(rotation)
      sin = numpy.sin(rotation)
      newx = scale*(x*cos - y*sin)/mag
      newy = scale*(x*sin + y*cos)/mag
      if return_ints:
        return int(newx), int(newy)
      else:
        return newx, newy

  def pixels_from_center_to_oculomatic(self, x, y):
    def f(o):
      ox, oy = o
      gx, gy = self.oculomatic_to_pixels_from_center(ox, oy, False)
      result = (gx - x)**2 + (gy - y)**2
      return result
    
    result = scipy.optimize.minimize(f, [x, y])
    return result.x

  def append_to_path(self, path, x, y):
    #Move to px, py, then draw an extremely short line.  With Round pen caps the result should be a circle the
    #size of the pen width.  If the pen is set to cosmetic then the circle size won't change with scale factors.
    path.moveTo(QPointF(x, y))
    path.lineTo(QPointF(x+1e-3, y))

  def add_training_sample(self, x, y, saccade_index: int):
    print('saccade_index', saccade_index)
    if saccade_index < 0 or saccade_index >= len(self.saccades):
      return
    
    min_distance, min_index = numpy.inf, -1
    for i, p in enumerate(self.points):
      px, py = self.oculomatic_to_pixels_from_center(p[0], p[1])
      distance = (px - x)**2 + (py - y)**2
      if distance < min_distance:
        min_distance = distance
        min_index = i

    if min_index == -1:
      return

    p = self.points[min_index]
    px, py = self.oculomatic_to_pixels_from_center(p[0], p[1])
    target = self.saccades[saccade_index]

    #target distance from center in meters
    from_center_m = target.x/self.dpi*2.54/100, target.y/self.dpi*2.54/100

    degrees = (180/numpy.pi*numpy.arctan2(from_center_m[0], self.distance_m),
               180/numpy.pi*numpy.arctan2(from_center_m[1], self.distance_m))

    point = self.points[min_index]
    training_length = len(self.training_data)
    def do():
      self.append_to_path(self.training_path, px, py)
      self.training_path.lineTo(target.x, target.y)
      self.training_data.append((point, degrees, target))
    def undo():
      self.training_data.pop()
      self.rebuild()
    return Action(do, undo)

  def rebuild(self):
    start = time.perf_counter()
    self.path.clear()
    self.training_path.clear()
    if self.get_model(self.model_name) is None:
      return

    for x, y in self.points:
      px, py = self.oculomatic_to_pixels_from_center(x, y)
      self.append_to_path(self.path, px, py)

    for p, _, target in self.training_data:
      px, py = self.oculomatic_to_pixels_from_center(p[0], p[1])
      self.append_to_path(self.training_path, px, py)
      if ((target.x - px)**2 + (target.y - py)**2)**.5 > 1:
        self.training_path.lineTo(target.x, target.y)
    end = time.perf_counter()
  
    edge = 3
    self.grid.clear()
    for ox in numpy.arange(-edge, edge + .1, .3):
      first = True
      for oy in numpy.arange(-edge, edge + .01, .3/5):
        px, py = self.oculomatic_to_pixels_from_center(ox, oy)
        if first:
          self.grid.moveTo(px, py)
          first = False
        else:
          self.grid.lineTo(px, py)

    for oy in numpy.arange(-edge, edge + .1, .3):
      first = True
      for ox in numpy.arange(-edge, edge + .01, .3/5):
        px, py = self.oculomatic_to_pixels_from_center(ox, oy)
        if first:
          self.grid.moveTo(px, py)
          first = False
        else:
          self.grid.lineTo(px, py)

    #for angle in numpy.arange(0, 2*numpy.pi, 2*numpy.pi/12):
    #  first = True
    #  for radius in numpy.arange(0, 5, .2):
    #    x, y = radius*numpy.cos(angle), radius*numpy.sin(angle)
    #    px, py = self.oculomatic_to_pixels_from_center(x, y)
    #    if first:
    #      self.grid.moveTo(px, py)
    #      first = False
    #    else:
    #      self.grid.lineTo(px, py)
#
    #for radius in numpy.arange(0, 5, 1):
    #  first = True
    #  for angle in numpy.arange(0, 2*numpy.pi, 2*numpy.pi/48):
    #    x, y = radius*numpy.cos(angle), radius*numpy.sin(angle)
    #    px, py = self.oculomatic_to_pixels_from_center(x, y)
    #    if first:
    #      self.grid.moveTo(px, py)
    #      first = False
    #    else:
    #      self.grid.lineTo(px, py)


    print('Rebuild duration', end - start)

  def reset(self):
    model = self.get_model(self.model_name)
    print('reset', self.model_name, model)
    if self.model_name == 'Projective':
      model.assign(DEFAULT_PROJECTIVE)
      pass
    elif self.model_name == 'Angular Scaling':
      old_pins = self.pins.copy()

      target_pins = [t.pin for t in self.saccades]
      def do():
        self.pins_updater.assign([], self.rebuild)
        for target in self.saccades:
          target.pin = -1
      def undo():
        self.pins_updater.assign(old_pins, self.rebuild)
        for target, p in zip(self.saccades, target_pins):
          target.pin = p

      return Action(do, undo)

  def start_nudge(self, start_point: typing.Tuple[int, int]):
    min_index, notch_nudge_index = self.get_start_nudge_index(start_point)
    if min_index == -1:
      return

    self.nudge_index = min_index
    self.notch_nudge_index = notch_nudge_index
    self.nudge_start_value = self.angular['Pins'][min_index].unwrap()

  def get_start_nudge_index(self, start_point: typing.Tuple[int, int]):
    model = self.get_model(self.model_name)
    if self.model_name != 'Angular Scaling':
      return -1, -1

    if self.pin_angles.size == 0:
      return -1, -1

    sx, sy = start_point
    mag = (sx**2 + sy**2)**.5
    start_angle = numpy.arctan2(sy, sx)
    if start_angle < 0:
      start_angle = 2*numpy.pi + start_angle

    min_distance, min_index, nudge_index = numpy.inf, -1, -1
    for i in range(self.pin_angles.shape[0]):
      model_angle = self.pin_angles[i, 0]
      rotation = self.pin_angles[i, 1]
      origin_angle = start_angle - rotation

      distance = model_angle - origin_angle
      if distance > numpy.pi:
        distance -= 2*numpy.pi
      elif distance < -numpy.pi:
        distance += 2*numpy.pi
      distance = abs(distance)

      if distance < min_distance:
        min_distance = distance
        min_index = i

        projected = numpy.cos(distance)*mag
        nudge_index = numpy.argmin(numpy.abs(self.pin_notches[i][1:,1] - projected))

    return min_index, nudge_index

  def stop_nudge(self):
    self.nudge_index = -1

  def nudge(self, start_point: typing.Tuple[int, int], current_point: typing.Tuple[int, int]):
    #print('self.current_saccade', self.current_saccade)
    if self.nudge_index == -1:
      return

    sx, sy = start_point
    cx, cy = current_point

    smag = (sx**2 + sy**2)**.5
    cmag = (cx**2 + cy**2)**.5
    delta_rotation = numpy.arctan2(cy, cx) - numpy.arctan2(sy, sx)
    if delta_rotation > numpy.pi:
      delta_rotation -= 2*numpy.pi
    if delta_rotation < -numpy.pi:
      delta_rotation += 2*numpy.pi
    delta_scale = cmag/smag

    old_pin = self.nudge_start_value
    old_notches = old_pin['Notches']
    old_notch = old_notches[self.notch_nudge_index]
    if self.notch_nudge_index+1 < len(old_pin['Notches']):
      next_notch = old_notches[self.notch_nudge_index+1]['Screen']
    else:
      next_notch = numpy.inf

    new_pin = copy.deepcopy(self.nudge_start_value)
    #print(new_pin['Rotation'], delta_rotation)
    new_pin['Rotation'] += delta_rotation
    scale = min(old_notch['Screen']*delta_scale, next_notch*.99)
    new_pin['Notches'][self.notch_nudge_index]['Screen'] = scale
    linked_target = None
    angle = new_pin['Angle'] + new_pin['Rotation']
    old_scale = old_notch['Screen']
    old_angle = old_pin['Angle'] + old_pin['Rotation']
    if old_notch.get('Init', False):
      for ocu, _, target in self.training_data:
        if target.pin == self.nudge_index:
          linked_target = target
          break

    if self.model_name == 'Angular Scaling':
      nudge_index = self.nudge_index
      #print(nudge_index, linked_target)
      def do():
        self.pins_updater.setitem(nudge_index, new_pin, self.rebuild)
        if linked_target is not None:
          linked_target.x, linked_target.y = int(scale*numpy.cos(angle)), int(scale*numpy.sin(angle))
      def undo():
        self.pins_updater.setitem(nudge_index, old_pin, self.rebuild)
        if linked_target is not None:
          linked_target.x, linked_target.y = int(old_scale*numpy.cos(old_angle)), int(old_scale*numpy.sin(old_angle))

      action = Action(do, undo)
      return action

  def optimize(self):
    model = self.get_model(self.model_name)
    if self.model_name == 'Projective':
      if not self.training_data:
        return
      
      # Fit projective
      n = len(self.training_data)
      A = numpy.zeros((2*n, 8))
      b = numpy.zeros(2*n)
      for i in range(n):
          x, y = self.training_data[i][0]
          tx, ty = self.training_data[i][1]
          A[2*i]   = [x, y, 1, 0, 0, 0, -tx*x, -tx*y]
          b[2*i]   = tx
          A[2*i+1] = [0, 0, 0, x, y, 1, -ty*x, -ty*y]
          b[2*i+1] = ty
      params, _, _, _ = numpy.linalg.lstsq(A, b, rcond=None)
      model['Parameters'].assign(params, self.rebuild)
    elif self.model_name == 'Angular Scaling':
      if not self.training_data:
        return
      
      old_pins = self.pins.copy()
      new_pins = []
      for ocu, deg, target in self.training_data:
        ocu_mag = (ocu[0]**2 + ocu[1]**2)**.5
        target_mag = (target.x**2 + target.y**2)**.5

        ocu_angle = numpy.arctan2(ocu[1], ocu[0])
        target_angle = numpy.arctan2(target.y, target.x)
        rotation = target_angle-ocu_angle

        if rotation > numpy.pi:
          rotation -= 2*numpy.pi
        elif rotation < -numpy.pi:
          rotation += 2*numpy.pi

        if ocu_angle < 0:
          ocu_angle = 2*numpy.pi + ocu_angle

        new_notch = {'Eye': ocu_mag, 'Screen': target_mag, 'Init': True}
        if target.pin >= 0:
          new_pin = old_pins[target.pin].unwrap()
          new_pin['Angle'] = ocu_angle
          new_pin['Rotation'] = rotation

          notches = new_pin['Notches']
          for notch in notches:
            if notch.get('Init', False):
              notch.update(new_notch)
              break
        else:
          new_pin = {
            'Angle': ocu_angle,
            'Rotation': rotation,
            'Notches': [new_notch]
          }

        new_pins.append(new_pin)

      angles = numpy.array([p['Angle'] for p in new_pins])
      sorted_indices = numpy.argsort(angles)

      old_target_pins = [t[2].pin for t in self.training_data]

      new_pins = [new_pins[i] for i in sorted_indices]

      def do():
        for i, d in enumerate(sorted_indices):
          self.training_data[d][2].pin = i
        self.pins_updater.assign(new_pins, self.rebuild)
      def undo():
        for i, d in zip(old_target_pins, self.training_data):
          _, _, target = d
          target.pin = i
        self.pins_updater.assign(old_pins, self.rebuild)
      return Action(do, undo)
    
  def add_notch(self, x, y):
    nudge_index, _ = self.get_start_nudge_index((x, y))
    if nudge_index != -1:
      mag = (x**2 + y**2)**.5
      insertion = bisect.bisect_left(self.pin_notches[nudge_index][1:,1], mag)

      if mag > self.pin_notches[nudge_index][-1,1]:
        num = self.pin_notches[nudge_index][-1,0] - self.pin_notches[nudge_index][-2,0]
        dem = self.pin_notches[nudge_index][-1,1] - self.pin_notches[nudge_index][-2,1]
        eye = mag*num/dem
      else:
        eye = numpy.interp(mag, self.pin_notches[nudge_index][:,1], self.pin_notches[nudge_index][:,0])

      
      old_pin = self.pins[nudge_index].unwrap()
      pin = copy.deepcopy(old_pin)
      pin['Notches'].insert(insertion, {
        'Screen': mag, 'Eye': eye, 'Init': False
      })

      return Action(
        lambda: self.pins_updater.setitem(nudge_index, pin),
        lambda: self.pins_updater.setitem(nudge_index, old_pin),
      )

  async def run(self):
    async def path_cleaner():
      while True:
        await asyncio.sleep(1)
        if self.hold:
          continue
        #Given a 60 Hz signal this is about 1 minute
        self.points = self.points[-600:]
        #print(len(self.points))
        self.rebuild()

    async def eye_loop():
      eye_stream = self.stub.analog(thalamus_pb2.AnalogRequest(node=thalamus_pb2.NodeSelector(type='OCULOMATIC')))
      async for m in eye_stream:
        x, y = None, None
        for span in m.spans:
          if span.name == 'X':
            x = m.data[span.begin]
          elif span.name == 'Y':
            y = m.data[span.begin]

        if x is None or y is None:
          continue

        y *= -1
        p = self.oculomatic_to_pixels_from_center(x, y)
        #if p in self.seen_points:
        #  continue
        #self.seen_points.add(p)

        self.points.append((x, y))
        #print('p', p)
        if p is None:
          continue
        px, py = p
        self.append_to_path(self.path, px, py)

    await asyncio.gather(path_cleaner(), eye_loop())
    #await asyncio.gather(eye_loop(), state_loop(self.stub), main_loop())

class SubjectView(QWidget):
  def __init__(self, task: Task):
    super().__init__()
    self.setWindowTitle('Subject')
    self.task = task

  def paintEvent(self, e):
    painter = QPainter(self)
    try:
      self.task.render(painter, self.size(), RenderOutput.SUBJECT)
    finally:
      painter.end()

  def closeEvent(self, e):
    self.task.stop()

class SubjectWindow(QMainWindow):
  def __init__(self, task: Task, view: QWidget):
    super().__init__()
    self.setWindowTitle('Subject')
    self.task = task

    self.setCentralWidget(view)
    self.central_widget = view
    
    self.is_fullscreen = False

    view_menu = self.menuBar().addMenu("&View")
    action = QAction('Enter &Fullscreen', self)
    action.setShortcut('Ctrl+F')
    action.triggered.connect(self.toggle_fullscreen)
    view_menu.addAction(action)

  def toggle_fullscreen(self) -> None: # pylint: disable=invalid-name
    """
    Toggle fullscreen mode
    """
    if self.is_fullscreen:
      self.showNormal()
      self.menuBar().show()
    else:
      self.showFullScreen()
      self.menuBar().hide()
      #self.central_widget.label.setText('Press Esc to exit fullscreen')
      #QTimer.singleShot(1000, lambda: self.central_widget.label.setText(''))
    self.is_fullscreen = not self.is_fullscreen

  def closeEvent(self, e):
    self.task.stop()

  def keyPressEvent(self, event: QKeyEvent) -> None: # pylint: disable=invalid-name
    """
    Toggle fullscreen when the user presses Esc
    """
    if event.key() == Qt.Key.Key_Escape:
      self.toggle_fullscreen()

class Action(typing.NamedTuple):
  do: typing.Callable[[], None]
  undo: typing.Callable[[], None]

class OperatorView(QWidget):
  def __init__(self, task: Task, subject_view: QWidget, config: dict):
    super().__init__()
    self.setWindowTitle('Operator')
    self.config = config
    self.task = task
    self.subject_view = subject_view
    #self.editing_saccades = False
    self.drag_start: QPoint | None = None
    self.base_eye_scaling: dict | None = None
    self.actions = []
    self.actions_position = 0
    self.setMouseTracking(True)
    self.last_nudge = None
    self.paint_time_sum = 0.0
    self.paint_time_count = 0
    self.paint_time_print = time.perf_counter()

  def contextMenuEvent(self, event):
    x, y = self.scale_to_subject(event.pos().x(), event.pos().y())

    def on_pin():
      saccade_index=self.task.current_saccade
      action = self.task.add_training_sample(x, y, saccade_index)
      if action is not None:
        self.do(action)

    def on_pin_here():
      self.do(self.task.pin_here(x, y))

    def on_notch():
      action = self.task.add_notch(x, y)
      if action is not None:
        self.do(action)

    menu = QMenu(self)

    action = QAction('Add Target', self)
    action.triggered.connect(
      lambda: self.do(Action(lambda: self.task.add_target(x, y), lambda: self.task.remove_target(x, y))))
    menu.addAction(action)

    action = QAction('Remove Target', self)
    action.triggered.connect(
      lambda: self.do(Action(lambda: self.task.remove_target(x, y), lambda: self.task.add_target(x, y))))
    menu.addAction(action)

    action = QAction('Pin', self)
    action.triggered.connect(on_pin)
    menu.addAction(action)

    action = QAction('Pin Here', self)
    action.triggered.connect(on_pin_here)
    menu.addAction(action)

    action = QAction('Add Notch', self)
    action.triggered.connect(on_notch)
    menu.addAction(action)

    menu.exec(event.globalPos())

  def paintEvent(self, e):
    start_time = time.perf_counter()
    painter = QPainter(self)
    try:
      painter.setBrush(Qt.GlobalColor.gray)
      painter.drawRect(0, 0, 10000, 10000)
      scale = min(self.width()/self.subject_view.width(), self.height()/self.subject_view.height())
      painter.scale(scale, scale)
      self.task.render(painter, self.subject_view.size(), RenderOutput.OPERATOR)
      if not self.hasFocus():
        font = painter.font()
        font.setPointSize(36)
        painter.setFont(font)
        metrics = QFontMetrics(font)
        painter.drawText(0,metrics.height(), "Unfocused")
    finally:
      painter.end()
      now = time.perf_counter()
      self.paint_time_sum += now - start_time
      self.paint_time_count += 1
      if now - self.paint_time_print >= 1:
        if self.paint_time_count:
          print('Paint duration', self.paint_time_sum/self.paint_time_count)
        self.paint_time_sum = 0.0
        self.paint_time_count = 0
        self.paint_time_print = now

  def keyPressEvent(self, a0: QKeyEvent):
    if a0.key() == Qt.Key.Key_Q:
      self.task.show_fixation = True
    elif a0.key() == Qt.Key.Key_W:
      self.task.show_saccade = True
    #elif a0.key() == Qt.Key.Key_E:
    #  self.editing_saccades = True
    #self.subject_view.update()
    #self.update()

  def do(self, action: Action, invoke = True):
    self.actions = self.actions[:self.actions_position]
    self.actions.append(action)
    self.actions_position += 1
    if invoke:
      action.do()

  def undo(self):
    if self.actions_position > 0:
      self.actions_position -= 1
      self.actions[self.actions_position].undo()

  def redo(self):
    if self.actions_position < len(self.actions):
      self.actions[self.actions_position].do()
      self.actions_position += 1

  def keyReleaseEvent(self, a0: QKeyEvent):
    if a0.key() == Qt.Key.Key_Q:
      self.task.show_fixation = False
    elif a0.key() == Qt.Key.Key_W:
      self.task.show_saccade = False
    elif a0.key() == Qt.Key.Key_R:
      asyncio.create_task(self.task.deliver_reward())
    elif a0.key() == Qt.Key.Key_Left:
      self.task.current_saccade = (self.task.current_saccade - 1) % len(self.task.saccades)
    elif a0.key() == Qt.Key.Key_Right:
      self.task.current_saccade = (self.task.current_saccade + 1) % len(self.task.saccades)
    elif a0.key() == Qt.Key.Key_Z:
      if a0.modifiers() & Qt.KeyboardModifier.ControlModifier:
        if a0.modifiers() & Qt.KeyboardModifier.ShiftModifier:
          self.redo()
        else:
          self.undo()
    elif a0.key() == Qt.Key.Key_Y:
      if a0.modifiers() & Qt.KeyboardModifier.ControlModifier:
        self.redo()

    #self.subject_view.update()
    #self.update()

  def scale_to_subject(self, x, y):
      scale = min(self.width()/self.subject_view.width(), self.height()/self.subject_view.height())
      return int(x/scale - self.subject_view.width()/2), int(y/scale - self.subject_view.height()/2)

  def mousePressEvent(self, event: QMouseEvent):
    if event.button() == Qt.MouseButton.LeftButton and event.modifiers() == Qt.KeyboardModifier.NoModifier: # type: ignore
      self.drag_start = self.scale_to_subject(event.pos().x(), event.pos().y())
      self.task.start_nudge(self.drag_start)

  def mouseReleaseEvent(self, event: QMouseEvent) -> None: # pylint: disable=invalid-name
    print(event, event.button())
    x, y = self.scale_to_subject(event.pos().x(), event.pos().y())
    print(x, y)
    self.drag_start = None
    self.task.stop_nudge()
    
    if self.last_nudge is not None:
      self.do(self.last_nudge, False)
      self.last_nudge = None

    #self.subject_view.update()
    #self.update()

  def mouseMoveEvent(self, event: QMouseEvent):
    self.task.mouse_pos = event.pos()
    self.task.mouse_pos_scaled = self.scale_to_subject(event.pos().x(), event.pos().y())
    if self.drag_start is not None:
      drag_current = self.scale_to_subject(event.pos().x(), event.pos().y())
      self.last_nudge = self.task.nudge(self.drag_start, drag_current)
      if self.last_nudge is not None:
        self.last_nudge.do()

  def optimize(self):
    action = self.task.optimize()
    if action is not None:
      self.do(action)

  def reset(self):
    action = self.task.reset()
    if action is not None:
      self.do(action)

  def closeEvent(self, e):
    self.task.stop()

class OperatorWindow(QMainWindow):
  def __init__(self, task: Task, view: QWidget, subject_view: QWidget, config: dict):
    super().__init__()
    self.task = task
    self.view = view
    view.setFocusPolicy(Qt.FocusPolicy.StrongFocus)

    central_widget = QWidget()

    layout = QVBoxLayout()
    layout.addWidget(view, 1)

    hwidget = QWidget()
    hlayout = QHBoxLayout()
    hlayout.addWidget(QLabel('Eye Opacity:'))
    eye_opacity_widget = QSlider(Qt.Orientation.Horizontal)
    eye_opacity_widget.setRange(0, 255)
    eye_opacity_widget.setValue(192)
    hlayout.addWidget(eye_opacity_widget)
    hwidget.setLayout(hlayout)
    layout.addWidget(hwidget)

    fixation_radius_widget = QSpinBox()
    fixation_radius_widget.setRange(0, 10000)
    saccade_radius_widget = QSpinBox()
    saccade_radius_widget.setRange(0, 10000)
    reward_widget = QSpinBox()
    reward_widget.setRange(0, 10000)
    reward_node_widget = QLineEdit()
    distance_widget = QDoubleSpinBox()
    default_scale_widget = QDoubleSpinBox()
    default_scale_widget.setRange(0, 10000)
    default_scale_widget.setValue(100.0)
    distance_widget.setRange(0, 10000)
    distance_widget.setDecimals(3)
    dpi_widget = QDoubleSpinBox()
    dpi_widget.setRange(0, 10000)
    clear_button = QPushButton('Clear')
    fit_button = QPushButton('Fit')
    reset_button = QPushButton('Reset')
    model_label = QLabel('Model:')
    hold = QCheckBox('Hold')

    layout.addWidget(model_label)

    layout2 = QHBoxLayout()
    layout2.addWidget(fit_button)
    layout2.addWidget(reset_button)
    layout.addLayout(layout2)

    layout2 = QGridLayout()
    layout2.addWidget(QLabel('Fixation Radius'), 0, 0)
    layout2.addWidget(fixation_radius_widget, 0, 1)
    layout2.addWidget(QLabel('Saccade Radius'), 0, 2)
    layout2.addWidget(saccade_radius_widget, 0, 3)

    layout2.addWidget(QLabel('Reward (ms)'), 1, 0)
    layout2.addWidget(reward_widget, 1, 1)
    layout2.addWidget(QLabel('Reward Node'), 1, 2)
    layout2.addWidget(reward_node_widget, 1, 3)

    layout2.addWidget(QLabel('Distance (m)'), 2, 0)
    layout2.addWidget(distance_widget, 2, 1)
    layout2.addWidget(QLabel('DPI'), 2, 2)
    layout2.addWidget(dpi_widget, 2, 3)

    layout2.addWidget(QLabel('Default Scale'), 3, 0)
    layout2.addWidget(default_scale_widget, 3, 1)
    layout2.addWidget(clear_button, 3, 2)
    layout2.addWidget(hold, 3, 3)

    layout.addLayout(layout2)

    central_widget.setLayout(layout)
    self.setCentralWidget(central_widget)
    central_widget.setFocusProxy(view)
    eye_scaling = config['eye_scaling']

    def on_opacity_changed(val):
      self.task.eye_opacity = val

    def on_hold(val):
      self.task.hold = val
    hold.toggled.connect(on_hold)

    def on_fixation_radius():
      val = fixation_radius_widget.value()
      eye_scaling['Fixation Radius'] = val
      self.task.fixation_radius = val
      view.setFocus()

    def on_saccade_radius():
      val = saccade_radius_widget.value()
      eye_scaling['Saccade Radius'] = val
      self.task.saccade_radius = val
      view.setFocus()

    def on_screen_distance(val):
      nonlocal models, projective
      if projective is not None:
        projective['Distance (m)'] = val

    def on_dpi(val):
      nonlocal models, projective
      if projective is not None:
        projective['DPI'] = val

    def on_default_scale(val):
      if angular is not None:
        angular['Scale Default'] = val
      self.task.rebuild()

    def on_clear():
      self.task.path.clear()
      #self.task.training_path.clear()
      del self.task.points[:]
      #del self.task.training_data[:]
      #self.view.actions = []
      #self.view.actions_position = 0
      self.task.seen_points.clear()

    def on_reward():
      val = reward_widget.value()
      eye_scaling['Reward (ms)'] = val
      self.task.reward_ms = val
      view.setFocus()

    def on_reward_node():
      eye_scaling['Reward Node'] = reward_node_widget.text()
      view.setFocus()

    eye_opacity_widget.valueChanged.connect(on_opacity_changed)
    fixation_radius_widget.editingFinished.connect(on_fixation_radius)
    saccade_radius_widget.editingFinished.connect(on_saccade_radius)
    reward_widget.editingFinished.connect(on_reward)
    clear_button.clicked.connect(on_clear)
    clear_button.setFocusPolicy(Qt.FocusPolicy.NoFocus)
    fit_button.clicked.connect(self.view.optimize)
    fit_button.setFocusPolicy(Qt.FocusPolicy.NoFocus)
    reset_button.clicked.connect(self.view.reset)
    reset_button.setFocusPolicy(Qt.FocusPolicy.NoFocus)
    distance_widget.valueChanged.connect(on_screen_distance)
    distance_widget.editingFinished.connect(view.setFocus)
    dpi_widget.valueChanged.connect(on_dpi)
    dpi_widget.editingFinished.connect(view.setFocus)
    default_scale_widget.valueChanged.connect(on_default_scale)
    default_scale_widget.editingFinished.connect(view.setFocus)
    reward_node_widget.editingFinished.connect(on_reward_node)
    
    if 'Fixation Radius' not in eye_scaling:
      eye_scaling['Fixation Radius'] = 50
    if 'Saccade Radius' not in eye_scaling:
      eye_scaling['Saccade Radius'] = 100
    if 'Reward (ms)' not in eye_scaling:
      eye_scaling['Reward (ms)'] = 500

    models, projective, angular = None, None, None
    def on_change(source, action, key, value):
      nonlocal models, projective, angular
      #print('OperatorWindow', source is projective, key, value)

      if source is eye_scaling:
        if key == 'Models':
          models = value
          value.recap()
        elif key == 'Selected Model':
          model_label.setText(f'Model: {value}')
        elif key == 'Reward Node':
          reward_node_widget.setText(value)
        elif key == 'Fixation Radius':
          fixation_radius_widget.setValue(value)
          self.task.fixation_radius = value
        elif key == 'Saccade Radius':
          saccade_radius_widget.setValue(value)
          self.task.saccade_radius = value
        elif key == 'Reward (ms)':
          reward_widget.setValue(value)
          self.task.reward_ms = value
      elif source is models:
        if key == 'Projective':
          projective = value
          value.recap()
        if key == 'Angular Scaling':
          angular = value
          value.recap()
      elif source is projective:
        if key == 'Distance (m)':
          distance_widget.setValue(value)
        elif key == 'DPI':
          dpi_widget.setValue(value)
      elif source is angular:
        if key == 'Scale Default':
          default_scale_widget.setValue(value)

    eye_scaling.add_recursive_observer(on_change)
    eye_scaling.recap()

  def closeEvent(self, e):
    self.task.stop()

  def keyReleaseEvent(self, a0: QKeyEvent):
    print(a0)

async def main():
  _ = QApplication(sys.argv)
  
  thread = ThalamusThread('localhost:50050')
  thread_task = await thread.async_start()
  pins = thread.config.get('eye_scaling', {}).get('Models', {}).get('Angular Scaling', {}).get('Pins', [])
  if len(pins) > 0 and not isinstance(pins[0], ObservableDict):
    pins.assign([])
  try:
    #thread.config['eye_scaling'].add_recursive_observer(print)
    #thread.config['eye_scaling'].recap()
    task = Task(thread.stub, thread.config)
    await task.prepare_reward()
    task.task = asyncio.create_task(task.run())
    subject = SubjectView(task)
    subject_window = SubjectWindow(task, subject)
    operator = OperatorView(task, subject, thread.config)
    operator_window = OperatorWindow(task, operator, subject, thread.config)

    subject_window.move(0, 0)
    subject_window.resize(800, 800)
    operator_window.move(800, 0)
    operator_window.resize(800, 800)
    subject_window.show()
    operator_window.show()

    while True:
      QApplication.processEvents()
      await asyncio.sleep(.1)
      if task.task.done():
        try:
          await task.task
        except asyncio.CancelledError:
          pass
        break
      subject.update()
      operator.update()
  finally:
    thread_task.cancel()


asyncio.run(main())