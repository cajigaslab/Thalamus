"""
Module for the Canvas QWidget the task will render into
"""

import json
import enum
import types
import base64
import typing
import logging
import pathlib
import asyncio
import hashlib
import itertools
import functools
import contextlib
import collections

import os.path

import stl.mesh
import numpy

import OpenGL.GL

from pkg_resources import resource_string, resource_filename

from ..config import ObservableCollection, ObservableDict
from .util import CanvasPainterProtocol, RenderOutput, voidptr, TaskContextProtocol, create_task_with_exc_handling
from .. import util_pb2
from .. import ophanim_pb2
from .. import ophanim_pb2_grpc
from .. import thalamus_pb2
from .. import thalamus_pb2_grpc
from ..qt import *
from .util import create_task_with_exc_handling
#from .. import recorder2_pb2
#from .. import recorder2_pb2_grpc

LOGGER = logging.getLogger(__name__)

VOLTAGE_RANGE = -10, 10
POINT_SIZE = 20

def load_transform(config: ObservableDict) -> QTransform:
  '''
  Load QTransform from config
  '''
  touch_transform = QTransform()
  if 'touch_transform' in config:
    transform_config = config['touch_transform']
    touch_transform.setMatrix(
      transform_config['m11'], transform_config['m12'], transform_config['m13'],
      transform_config['m21'], transform_config['m22'], transform_config['m23'],
      transform_config['m31'], transform_config['m32'], transform_config['m33'])
  return touch_transform

class Opcode(enum.IntEnum):
  """
  WebSocket opcodes
  """
  TEXT = 1
  BINARY = 2
  CLOSE = 8
  PING = 9
  PONG = 10

MIME_TYPES = collections.defaultdict(lambda: 'application/octet-stream', {
  '.css': 'text/css',
  '.html': 'text/html',
  '.js': 'text/javascript',
})


VERTEX_SHADER_SOURCE = """
  attribute vec4 vertex;
  attribute vec3 normal;

  uniform mat4 projMatrix;
  uniform mat4 mvMatrix;
  uniform mat3 normalMatrix;
  uniform vec4 color;

  varying vec4 fragColor;
  void main() {
    vec4 model = mvMatrix * vertex;
    vec3 tnormal = normalMatrix * normal;

    //vec4 lightPos = vec4(0, 0, 100, 1);
    //float scale = abs(dot(normalize(lightPos.xyz-model.xyz), tnormal));

    vec3 lightNormal = vec3(0, 0, 1);
    float scale = abs(dot(lightNormal, normalize(tnormal)));

    fragColor = vec4((color*scale).xyz, 1);
    gl_Position = projMatrix * model;
  }
"""

FRAGMENT_SHADER_SOURCE = """
  varying vec4 fragColor;
  void main() {
     gl_FragColor = fragColor;
  }
"""

class GlslLocations(typing.NamedTuple):
  """
  Collection of parameters for OpenGL pipeline
  """
  vertex: int
  normal: int
  color: int
  model_view_matrix: int
  projection_matrix: int
  normal_matrix: int

class Vbos(typing.NamedTuple):
  """
  Vertex buffers used in rendering
  """
  vertex: QOpenGLBuffer
  normal: QOpenGLBuffer

class OpenGLConfig(typing.NamedTuple):
  '''
  OpenGL settings
  '''
  projection_matrix: QMatrix4x4
  program: QOpenGLShaderProgram
  locations: GlslLocations
  vbo_cache: typing.Dict[int, Vbos]

class BrowserReflectingPainter(QPainter):
  """
  Forwards draw commands to the browser
  """

  def __init__(self, send: typing.Callable[[typing.Any], None],
               *args: typing.Any) -> None:
    super().__init__(*args)
    self.send = send

  def __enter__(self) -> 'BrowserReflectingPainter':
    self.send({'function': '__enter__'})
    return self

  def __exit__(self, exc_type: type, exc_value: Exception, exc_traceback: types.TracebackType) -> None:
    self.send({'function': '__exit__'})

  def fillRect(self, *args: typing.Any, **_: typing.Any) -> None: # pylint: disable=invalid-name
    """
    QPainter.fillRect override
    """
    signature = tuple(type(arg) for arg in args)
    if signature == (QRect, QColor):
      rect = args[0]
      color = args[1]
    elif signature == (int, int, int, int, QColor):
      rect = QRect(args[0], args[1], args[2], args[3])
      color = args[4]
    elif signature == (int, int, int, int, Qt.GlobalColor):
      rect = QRect(args[0], args[1], args[2], args[3])
      color = QColor(args[4])
    else:
      raise NotImplementedError(f'BrowserReflectingPainter.fillRect{signature} is not supported')

    super().fillRect(rect, color)

    self.send({
      'function': 'fillRect',
      'args': [
        rect.x(),
        rect.y(),
        rect.width(),
        rect.height(),
        [color.red(), color.green(), color.blue()]
      ]
    })

  def drawImage(self, *args: typing.Any, **_: typing.Any) -> None:
    return super().drawImage(*args, **_)
    signature = tuple(type(arg) for arg in args)
    if signature == (QRect, QImage):
      rect = args[0]
      image = args[1]
    else:
      assert(False, 'Unsupport drawImage call signature ' + str(args))

    super().drawImage(rect, image)
    self.send({
      'function': 'drawImage',
      'args': [rect.x(), rect.y(), rect.width(), rect.height(), id(image)]
    })

class CanvasPainter(QPainter):
  """
  Extends QPainter with ability to selectively render to subject or operator views.  Also provides addition functions
  for OpenGL rendering
  """
  def __init__(self, output_mask: RenderOutput,
               opengl_config: OpenGLConfig, *args: typing.Any) -> None:
    super().__init__(*args)
    self.current_output_mask = RenderOutput.ANY
    self.output_mask = output_mask
    self.model_view = QMatrix4x4()
    self.projection_matrix = opengl_config.projection_matrix
    self.program = opengl_config.program
    self.locations = opengl_config.locations
    self.vbo_cache = opengl_config.vbo_cache

  def fillRect(self, *args: typing.Any, **kwargs: typing.Any) -> None: # pylint: disable=invalid-name
    '''
    Override that implements masked rendering
    '''
    if self.current_output_mask in (RenderOutput.ANY, self.output_mask):
      super().fillRect(*args, **kwargs)

  def fillPath(self, *args: typing.Any, **kwargs: typing.Any) -> None: # pylint: disable=invalid-name
    '''
    Override that implements masked rendering
    '''
    if self.current_output_mask in (RenderOutput.ANY, self.output_mask):
      super().fillPath(*args, **kwargs)

  def drawPath(self, *args: typing.Any, **kwargs: typing.Any) -> None: # pylint: disable=invalid-name
    '''
    Override that implements masked rendering
    '''
    if self.current_output_mask in (RenderOutput.ANY, self.output_mask):
      super().drawPath(*args, **kwargs)

  def drawImage(self, *args: typing.Any, **kwargs: typing.Any) -> None: # pylint: disable=invalid-name
    '''
    Override that implements masked rendering
    '''
    if self.current_output_mask in (RenderOutput.ANY, self.output_mask):
      super().drawImage(*args, **kwargs)

  @contextlib.contextmanager
  def masked(self, mask: RenderOutput) -> typing.Iterator['CanvasPainterProtocol']:
    '''
    Context manager that will disable rendering if we are not rendering to the specified output
    '''
    previous_mask = self.current_output_mask
    self.current_output_mask = mask
    try:
      yield self
    finally:
      self.current_output_mask = previous_mask

  def render_stl(self, mesh: stl.mesh.Mesh, color: QColor) -> None:
    '''
    Draw an STL mesh
    '''
    if self.current_output_mask not in (RenderOutput.ANY, self.output_mask):
      return

    super().beginNativePainting()

    OpenGL.GL.glEnable(OpenGL.GL.GL_DEPTH_TEST)

    self.program.bind()

    if not id(mesh) in self.vbo_cache:
      vertex = numpy.array(mesh.points.flatten(), dtype=numpy.float32)
      vertex_vbo = QOpenGLBuffer()
      vertex_vbo.create()
      vertex_vbo.bind()
      vertex_vbo.allocate(typing.cast(voidptr, vertex.data), vertex.nbytes)
      vertex_vbo.release()

      normal = numpy.array(mesh.normals.repeat(3, axis=0).flatten(), dtype=numpy.float32)
      normal_vbo = QOpenGLBuffer()
      normal_vbo.create()
      normal_vbo.bind()
      normal_vbo.allocate(typing.cast(voidptr, normal.data), normal.nbytes)
      normal_vbo.release()
      new_vbos = Vbos(vertex_vbo, normal_vbo)

      self.vbo_cache[id(mesh)] = new_vbos
    vbo_vertex, vbo_normal = self.vbo_cache[id(mesh)].vertex, self.vbo_cache[id(mesh)].normal

    self.program.setUniformValue(self.locations.model_view_matrix, self.model_view)
    self.program.setUniformValue(self.locations.normal_matrix, self.model_view.normalMatrix())
    self.program.setUniformValue(self.locations.projection_matrix, self.projection_matrix)
    self.program.setUniformValue(self.locations.color, color)

    vbo_vertex.bind()
    OpenGL.GL.glEnableVertexAttribArray(self.locations.vertex)
    OpenGL.GL.glVertexAttribPointer(self.locations.vertex, 3, OpenGL.GL.GL_FLOAT, OpenGL.GL.GL_FALSE, 0, None)

    vbo_normal.bind()
    OpenGL.GL.glEnableVertexAttribArray(self.locations.normal)
    OpenGL.GL.glVertexAttribPointer(self.locations.normal, 3, OpenGL.GL.GL_FLOAT, OpenGL.GL.GL_FALSE, 0, None)

    OpenGL.GL.glDrawArrays(OpenGL.GL.GL_TRIANGLES, 0, mesh.points.size//3)

    vbo_vertex.release()
    vbo_normal.release()

    super().endNativePainting()

class TouchCalibration():
  '''
  Touch calibration fields
  '''
  def __init__(self, config) -> None:
    self.calibrating_touch = False
    self.touch_target_voltage_count = 0
    self.touch_target_index = 0
    self.touch_targets: typing.List[typing.List[int]] = []
    self.touch_target_voltage: typing.List[typing.List[float]] = []
    self.touch_transform = load_transform(config)

  def __str__(self) -> str:
    return f'TouchCalibration(calibrating_touch={self.calibrating_touch})'

class InputConfig():
  '''
  Touch and eye input config
  '''
  def __init__(self, config: ObservableCollection) -> None:
    self.touch_channels = config['touch_channels']
    self.touch_path = QPainterPath()
    self.touch_path.setFillRule(Qt.FillRule.WindingFill)
    self.last_touch = QPoint(0, 0)
    self.gaze_paths = [
      QPainterPath(), QPainterPath(), QPainterPath(), QPainterPath()]
    self.gaze_paths[0].setFillRule(Qt.FillRule.WindingFill)
    self.gaze_paths[1].setFillRule(Qt.FillRule.WindingFill)
    self.gaze_paths[2].setFillRule(Qt.FillRule.WindingFill)
    self.gaze_paths[3].setFillRule(Qt.FillRule.WindingFill)
    self.points: typing.List[typing.List[QPointF]] = [[], [], [], []]
    self.gaze_transforms = [
      QTransform(),
      QTransform(),
      QTransform(),
      QTransform()]
    self.touch_calibration = TouchCalibration(config)

    if 'eye_scaling' not in config:
      config['eye_scaling'] = {}
    for quadrant in ["I", "II", "III", "IV"]:
      if quadrant not in config['eye_scaling']:
        config['eye_scaling'][quadrant] = {'x': 1, 'y': 1}
      config['eye_scaling'][quadrant].add_observer(functools.partial(self.on_eye_scaling_changed, quadrant))
      self.on_eye_scaling_changed(quadrant, ObservableCollection.Action.SET, 'x', config['eye_scaling'][quadrant]['x'])
      self.on_eye_scaling_changed(quadrant, ObservableCollection.Action.SET, 'y', config['eye_scaling'][quadrant]['y'])

  def on_eye_scaling_changed(self, quadrant: str, _: ObservableCollection.Action,
                             key: typing.Any, value: typing.Any) -> None:
    '''
    Updates the UI in response to gaze scaling changes
    '''
    if quadrant == 'I':
      transform = self.gaze_transforms[0]
      points = self.points[0]
      path = self.gaze_paths[0] = QPainterPath()
    elif quadrant == 'II':
      transform = self.gaze_transforms[1]
      points = self.points[1]
      path = self.gaze_paths[1] = QPainterPath()
    elif quadrant == 'III':
      transform = self.gaze_transforms[2]
      points = self.points[2]
      path = self.gaze_paths[2] = QPainterPath()
    elif quadrant == 'IV':
      transform = self.gaze_transforms[3]
      points = self.points[3]
      path = self.gaze_paths[3] = QPainterPath()

    transform.setMatrix(
      value if key == 'x' else transform.m11(),                          transform.m12(), transform.m13(),
                               transform.m21(), value if key == 'y' else transform.m22(), transform.m23(),
                               transform.m31(),                          transform.m32(), transform.m33())
    path.setFillRule(Qt.FillRule.WindingFill)
    for point in points:
      scaled_point = transform.map(point)
      path.addEllipse(scaled_point, POINT_SIZE, POINT_SIZE)

  def __str__(self) -> str:
    return f'InputConfig(touch_channels={self.touch_channels})'

class CanvasOpenGLConfig(typing.NamedTuple):
  '''
  OpenGL properties of the Canvas
  '''
  program: QOpenGLShaderProgram
  proj_matrix_loc: int
  mv_matrix_loc: int
  normal_matrix_loc: int
  color_loc: int
  vbo_cache: typing.Dict[int, Vbos]
  proj: QMatrix4x4

TOUCH_LISTENER = 0
GAZE_LISTENER = 1
RENDERER = 2

class Listeners():
  '''
  Canvas listeners
  '''
  def __init__(self) -> None:
    self.touch_listener: typing.Callable[[QPoint], None] = lambda e: None
    self.gaze_listener: typing.Callable[[QPoint], None] = lambda e: None
    self.renderer: typing.Callable[[CanvasPainterProtocol], None] = lambda w: None
    self.key_release_handler: typing.Callable[[CanvasPainterProtocol], None] = lambda w: None
    self.paint_subscribers: typing.List[typing.Callable[[], None]] = []

  def __str__(self) -> str:
    return str((self.touch_listener, self.gaze_listener, self.renderer))

  def __repr__(self) -> str:
    return str(self)

class Handles:
  '''
  Resouces that will need to be cleaned up
  '''
  def __init__(self) -> None:
    self.server: typing.Optional[asyncio.AbstractServer] = None
    self.clear_loop: typing.Optional[asyncio.Task[typing.Any]] = None

  def __str__(self) -> str:
    return str((self.server, self.clear_loop))

  def __repr__(self) -> str:
    return str(self)

class Canvas(QOpenGLWidget):
  """
  The QWidget the task will render on and that will generate mouse events on touch input
  """
  def __init__(self, config: ObservableCollection,
               recorder: typing.Any,
               ophanim: ophanim_pb2_grpc.OphanimStub,
               thalamus: thalamus_pb2_grpc.ThalamusStub,
               port: typing.Optional[int] = None,
               mock_sleep: typing.Optional[typing.Callable[[float], 'asyncio.Future[None]']] = None) -> None:
    super().__init__()
    self.config = config

    self.thalamus = thalamus

    self.sent_images = set()
    self.recorder = recorder
    self.ophanim = ophanim
    self.task_context = None
    if mock_sleep:
      self.asyncio_sleep = mock_sleep
    else:
      self.asyncio_sleep = asyncio.sleep

    self.browser_dimensions: typing.Optional[typing.Tuple[int, int]] = None

    self.current_output_mask = RenderOutput.SUBJECT

    request = thalamus_pb2.AnalogRequest(node=thalamus_pb2.NodeSelector(type='OCULOMATIC'), channel_names=['X','Y'])
    create_task_with_exc_handling(self.on_ros_gaze(thalamus.analog(request)))
    request = thalamus_pb2.AnalogRequest(node=thalamus_pb2.NodeSelector(type='TOUCH_SCREEN'), channel_names=['X','Y'])
    create_task_with_exc_handling(self.on_ros_touch(thalamus.analog(request)))

    self.handles = Handles()
    if port:
      asyncio.get_event_loop().create_task(self.__start_server(port))
    self.input_config = InputConfig(config)
    self.websockets: typing.List[asyncio.StreamWriter] = []

    self.listeners = Listeners()

    self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)

    self.opengl_config: typing.Optional[CanvasOpenGLConfig] = None

    self.handles.clear_loop = asyncio.get_event_loop().create_task(self.__clear_periodically())

  def set_task_context(self, task_context: TaskContextProtocol):
    self.task_context = task_context

  def width(self):
    return self.browser_dimensions[0] if self.browser_dimensions else super().width()

  def height(self):
    return self.browser_dimensions[1] if self.browser_dimensions else super().height()

  async def __clear_periodically(self) -> None:
    while True:
      await self.asyncio_sleep(60)
      self.clear_accumulation()

  def cleanup(self) -> None:
    '''
    Clean up tasks and servers
    '''
    if self.handles.clear_loop:
      self.handles.clear_loop.cancel()
    if self.handles.server:
      self.handles.server.close()

  @property
  def renderer(self) -> typing.Callable[[CanvasPainterProtocol], None]:
    '''
    Get renderer callback
    '''
    return self.listeners.renderer

  @renderer.setter
  def renderer(self, value: typing.Callable[[CanvasPainterProtocol], None]) -> None:
    self.listeners.renderer = value

  @property
  def key_release_handler(self) -> typing.Callable[[CanvasPainterProtocol], None]:
    '''
    Get key_release_handler callback
    '''
    return self.listeners.key_release_handler

  @key_release_handler.setter
  def key_release_handler(self, value: typing.Callable[[CanvasPainterProtocol], None]) -> None:
    self.listeners.key_release_handler = value

  @property
  def touch_listener(self) -> typing.Callable[[QPoint], None]:
    '''
    Get touch callback
    '''
    return self.listeners.touch_listener

  @touch_listener.setter
  def touch_listener(self, value: typing.Callable[[QPoint], None]) -> None:
    self.listeners.touch_listener = value

  @property
  def gaze_listener(self) -> typing.Callable[[QPoint], None]:
    '''
    Get gaze callback
    '''
    return self.listeners.gaze_listener

  @gaze_listener.setter
  def gaze_listener(self, value: typing.Callable[[QPoint], None]) -> None:
    self.listeners.gaze_listener = value

  @contextlib.contextmanager
  def masked(self, mask: RenderOutput) -> typing.Iterator['Canvas']:
    '''
    Context manager that will disable rendering if we are not rendering to the specified output
    '''
    previous_mask = self.current_output_mask
    self.current_output_mask = mask
    try:
      yield self
    finally:
      self.current_output_mask = previous_mask

  def initializeGL(self) -> None: # pylint: disable=invalid-name
    '''
    Sets up OpenGL
    '''
    program = QOpenGLShaderProgram()
    program.addShaderFromSourceCode(QOpenGLShader.ShaderTypeBit.Vertex, VERTEX_SHADER_SOURCE)
    program.addShaderFromSourceCode(QOpenGLShader.ShaderTypeBit.Fragment, FRAGMENT_SHADER_SOURCE)
    program.bindAttributeLocation("vertex", 0)
    program.bindAttributeLocation("normal", 1)
    program.link()

    program.bind()
    proj_matrix_loc = program.uniformLocation("projMatrix")
    mv_matrix_loc = program.uniformLocation("mvMatrix")
    normal_matrix_loc = program.uniformLocation("normalMatrix")
    color_loc = program.uniformLocation("color")

    self.opengl_config = CanvasOpenGLConfig(
      program,
      proj_matrix_loc,
      mv_matrix_loc,
      normal_matrix_loc,
      color_loc,
      {},
      QMatrix4x4())

  def resizeGL(self, width: int, height: int) -> None: # pylint: disable=invalid-name
    '''
    Updates perspective matrix as view changes
    '''
    assert self.opengl_config, 'opengl_config is None'

    self.opengl_config.proj.setToIdentity()
    self.opengl_config.proj.perspective(45.0, width / height, 0.01, 100.0)

  def paintGL(self) -> None: # pylint: disable=invalid-name
    '''
    Paints the task
    '''
    assert self.opengl_config, 'opengl_config is None'

    locations = GlslLocations(0, 1, self.opengl_config.color_loc, self.opengl_config.mv_matrix_loc,
                              self.opengl_config.proj_matrix_loc, self.opengl_config.normal_matrix_loc)
    geometry = qt_screen_geometry()
    painter = CanvasPainter(self.current_output_mask,
                            OpenGLConfig(self.opengl_config.proj, self.opengl_config.program, locations,
                                         self.opengl_config.vbo_cache), self)
    with painter:
      painter.fillRect(QRect(0, 0, 4000, 4000), QColor(0, 0, 0))
      self.listeners.renderer(painter)

      with painter.masked(RenderOutput.OPERATOR):
        painter.fillPath(self.input_config.touch_path, QColor(255, 0, 0))

        painter.setTransform(QTransform.fromTranslate(self.width()/2, self.height()/2))
        for path in self.input_config.gaze_paths:
          painter.fillPath(path, QColor(0, 0, 255))

    if self.current_output_mask != RenderOutput.OPERATOR:
      for subscriber in self.listeners.paint_subscribers:
        subscriber()

  async def __start_server(self, port: int) -> None:
    """
    Starts the HTTP server
    """
    self.handles.server = await asyncio.start_server(self.on_client_connected, None, port)

  async def on_websocket(self, headers: typing.Dict[str, str],
                         reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    """
    Processes WebSockets
    """
    #print(headers['sec-websocket-key'])
    hasher = hashlib.sha1()
    hasher.update(f"{headers['sec-websocket-key']}258EAFA5-E914-47DA-95CA-C5AB0DC85B11".encode())
    writer.write(b"HTTP/1.1 101 Switching Protocols\r\n")
    writer.write(b"Upgrade: websocket\r\n")
    writer.write(b"Connection: Upgrade\r\n")
    writer.write(f"Sec-WebSocket-Accept: {base64.b64encode(hasher.digest()).decode()}\r\n".encode())
    writer.write(b"\r\n")

    self.websockets.append(writer)
    self.sent_images.clear()
    try:
      while True:
        buffer = await reader.readexactly(1)
        opcode = buffer[0] & 0x0F

        buffer = await reader.readexactly(1)
        is_masked = buffer[0] & 0x80
        payload_length = buffer[0] & 0x7F
        remaining_payload_length_bytes = 0
        if payload_length == 126:
          payload_length = 0
          remaining_payload_length_bytes = 2
        elif payload_length == 127:
          payload_length = 0
          remaining_payload_length_bytes = 8

        buffer = await reader.readexactly(remaining_payload_length_bytes)
        for byte in buffer:
          payload_length = (payload_length << 8) + byte


        masking_key = await reader.readexactly(4) if is_masked else b'\x00'

        buffer = await reader.readexactly(payload_length)
        buffer = bytes(a ^ b for a, b in zip(buffer, itertools.cycle(masking_key)))

        if opcode in (1, 2):
          #print(buffer)
          parsed = json.loads(buffer)
          if parsed['type'] == 'cursor':
            if parsed.get('buttons', 0) & 1:
              self.on_touch(QPoint(parsed['x'], parsed['y']))
            else: 
              self.on_touch(QPoint(-1, -1))
            if parsed.get('buttons', 0) & 2:
              self.on_gaze(QPoint(parsed['x'], parsed['y']))
            else: 
              self.on_gaze(QPoint(-1, -1))
          elif parsed['type'] == 'dimensions':
            self.browser_dimensions = parsed['width'], parsed['height']

    except asyncio.IncompleteReadError:
      pass
    finally:
      self.websockets.remove(writer)


  async def on_client_connected(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    """
    Processes HTTP file and WebSocket requests
    """
    text = await reader.readuntil(b'\r\n\r\n')
    lines = text.decode().strip().split('\r\n')
    request_line = lines[0]
    request_tokens = request_line.split(' ')
    method, path = request_tokens[0], request_tokens[1]

    temp = (line.split(':', 1) for line in lines[1:])
    headers = dict((k.lower().strip(), v.strip()) for (k, v) in temp)
    #print(request_line)
    #print(headers)

    if method == 'GET':
      if 'upgrade' not in headers or headers['upgrade'].lower() != 'websocket':
        path = '/index.html' if path == '/' else path
        mime_type = MIME_TYPES[os.path.splitext(path)[1]]
        try:
          stream = resource_string(__name__, f'browser{path}')
        except FileNotFoundError:
          writer.write(b'HTTP/1.1 404 Not Found\r\n')
          writer.write(b'\r\n')
          return

        writer.write(b'HTTP/1.1 200 OK\r\n')
        writer.write(f'Content-Type: {mime_type}\r\n'.encode())
        writer.write(b'\r\n')
        writer.write(stream)

        writer.close()
      else:
        await self.on_websocket(headers, reader, writer)
        
  def load_images(self, images: typing.List[QImage]):
    #self.send({'function': 'clear_images'})
    for image in (image for image in images if id(image) not in self.sent_images):
      buffer = QBuffer()
      image.save(buffer, 'png')
      encoded = base64.b64encode(buffer.buffer()).decode()
      url = f'data:image/png;base64,{encoded}'
      self.send({
        'function': 'store_image',
        'args': [id(image), url]
      })
      self.sent_images.add(id(image))

  def send(self, message: typing.Any) -> None:
    """
    Sends a message to connected websocket clients
    """
    if not isinstance(message, bytes):
      message = json.dumps(message).encode()

    length = len(message)
    if length < 126:
      header = bytes([
        0x80 | int(Opcode.TEXT),
        length
      ])
    elif 126 <= length < 2**16:
      header = bytes([
        0x80 | int(Opcode.TEXT),
        126,
        (length >> 8) & 0x00FF,
        (length >> 0) & 0x00FF
      ])
    else:
      header = bytes([
        0x80 | int(Opcode.TEXT),
        127,
        (length >> 56) & 0x00FF,
        (length >> 48) & 0x00FF,
        (length >> 40) & 0x00FF,
        (length >> 32) & 0x00FF,
        (length >> 24) & 0x00FF,
        (length >> 16) & 0x00FF,
        (length >> 8) & 0x00FF,
        (length >> 0) & 0x00FF
      ])
    #print(header + message)
    #print(self.websockets)
    for websocket in self.websockets:
      websocket.write(header)
      websocket.write(message)

    #if self.websockets:
    #  await asyncio.wait([w.drain() for w in self.websockets])

  async def on_ros_touch(self, messages: typing.AsyncIterable[thalamus_pb2.AnalogResponse]) -> None:
    """
    Translates touch events into mouse input
    """
    # TODO: check if this indexing is correct, because it seems wrong.
    try:
      x, y = 0.0, 0.0
      async for message in messages:
        for span in message.spans:
          if span.name == 'X' and span.begin < span.end:
            x = message.data[span.end-1]
          elif span.name == 'Y' and span.begin < span.end:
            y = message.data[span.end-1]
        
        voltage = QPointF(x, y)
        if voltage.x() < -5 or voltage.y() < -5:
          self.on_touch(QPoint(-1, -1))
          continue
        self.last_voltage = voltage

        global_point = voltage
        local_point = self.mapFromGlobal(QPoint(int(global_point.x()), int(global_point.y())))

        self.on_touch(local_point)
    except asyncio.CancelledError:
      pass

  def on_touch(self, point: QPoint) -> None:
    """
    Core of touch event processing
    """
    offset = point - self.input_config.last_touch
    if QPoint.dotProduct(offset, offset) > 2:
      self.input_config.touch_path.addEllipse(QPointF(point), POINT_SIZE, POINT_SIZE)
      self.input_config.last_touch = offset

    self.listeners.touch_listener(point)
    if self.task_context:
      self.task_context.process()

  def on_gaze(self, point: QPoint) -> None:
    """
    Core of gaze event processing
    """
    self.listeners.gaze_listener(point)
    if self.task_context:
      self.task_context.process()

  async def on_ros_gaze(self, messages: typing.AsyncIterable[thalamus_pb2.AnalogResponse]) -> None:
    """
    Processes eye input
    """
    async for message in messages:
      x, y = None, None
      for span in message.spans:
        if span.name == 'X' and span.begin < span.end:
          x = message.data[span.end-1]
        elif span.name == 'Y' and span.begin < span.end:
          y = message.data[span.end-1]
      assert x is not None and y is not None

      voltage_point = QPointF(x, -y)

      if y >= 0:
        if x >= 0:
          self.input_config.points[0].append(voltage_point)
          scaled_point = self.input_config.gaze_transforms[0].map(voltage_point)
          self.input_config.gaze_paths[0].addEllipse(scaled_point, POINT_SIZE, POINT_SIZE)
        else:
          self.input_config.points[1].append(voltage_point)
          scaled_point = self.input_config.gaze_transforms[1].map(voltage_point)
          self.input_config.gaze_paths[1].addEllipse(scaled_point, POINT_SIZE, POINT_SIZE)
      else:
        if x < 0:
          self.input_config.points[2].append(voltage_point)
          scaled_point = self.input_config.gaze_transforms[2].map(voltage_point)
          self.input_config.gaze_paths[2].addEllipse(scaled_point, POINT_SIZE, POINT_SIZE)
        else:
          self.input_config.points[3].append(voltage_point)
          scaled_point = self.input_config.gaze_transforms[3].map(voltage_point)
          self.input_config.gaze_paths[3].addEllipse(scaled_point, POINT_SIZE, POINT_SIZE)

      #geometry = qt_screen_geometry()
      #global_point = QPoint(scaled_point.x() + geometry.width()/2, scaled_point.y() + geometry.height()/2)
      #local_point = self.mapFromGlobal(global_point)

      local_point = QPoint(int(scaled_point.x()) + self.width()//2, int(scaled_point.y()) + self.height()//2)

      self.on_gaze(local_point)

  def keyReleaseEvent(self, e: QKeyEvent) -> None: # pylint: disable=invalid-name
    '''
    Progresses touch calibration on key presses
    '''
    self.listeners.key_release_handler(e)

  def clear_accumulation(self) -> None:
    '''
    Clears the accumulation view
    '''
    self.input_config.gaze_paths = [
      QPainterPath(), QPainterPath(), QPainterPath(), QPainterPath()]
    self.input_config.gaze_paths[0].setFillRule(Qt.FillRule.WindingFill)
    self.input_config.gaze_paths[1].setFillRule(Qt.FillRule.WindingFill)
    self.input_config.gaze_paths[2].setFillRule(Qt.FillRule.WindingFill)
    self.input_config.gaze_paths[3].setFillRule(Qt.FillRule.WindingFill)
    self.input_config.points = [[], [], [], []]

    self.input_config.touch_path = QPainterPath()
    self.input_config.touch_path.setFillRule(Qt.FillRule.WindingFill)

  def mousePressEvent(self, event: QMouseEvent) -> None: # pylint: disable=invalid-name
    self.mouseMoveEvent(event)

  def mouseReleaseEvent(self, event: QMouseEvent) -> None: # pylint: disable=invalid-name
    if not(event.buttons() & Qt.MouseButton.LeftButton): # type: ignore
      self.on_touch(QPoint(-1, -1))

  def mouseMoveEvent(self, event: QMouseEvent) -> None: # pylint: disable=invalid-name
    """
    Forwards mouse events to the current mouse_listener
    """
    if event.buttons() & Qt.MouseButton.LeftButton: # type: ignore
      self.on_touch(event.pos())
    if event.buttons() & Qt.MouseButton.RightButton: # type: ignore

      local_point = event.pos()
      from_center = local_point - QPoint(self.width()//2, self.height()//2)

      from_center.setY(-from_center.y())

      if from_center.y() >= 0:
        if from_center.x() >= 0:
          transform = self.input_config.gaze_transforms[0]
        else:
          transform = self.input_config.gaze_transforms[1]
      else:
        if from_center.x() < 0:
          transform = self.input_config.gaze_transforms[2]
        else:
          transform = self.input_config.gaze_transforms[3]

      gaze_message = thalamus_pb2.AnalogResponse(
          data = [from_center.x()/transform.m11(), from_center.y()/transform.m22()],
          spans = [thalamus_pb2.Span(name='X', begin=0, end=1), thalamus_pb2.Span(name='Y', begin=1, end=2)]
      )

      async def async_yield(message):
        yield message

      create_task_with_exc_handling(self.on_ros_gaze(async_yield(gaze_message)))

