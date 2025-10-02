try:
  import cv2
except ImportError:
  print('cv2 import failed, yuv video unavailable')

import sys
import grpc
import json
import numpy
import typing
import asyncio
import argparse
import datetime
import traceback
import functools

from .thread import ThalamusThread
from .util import MeteredUpdater, IterableQueue
from .task_controller.util import create_task_with_exc_handling
from .config import ObservableDict

from . import  thalamus_pb2
from . import thalamus_pb2_grpc

from .qt import *

QTKEY_TO_CODE = {
    Qt.Key.Key_0: 'Digit0',
    Qt.Key.Key_1: 'Digit1',
    Qt.Key.Key_2: 'Digit2',
    Qt.Key.Key_3: 'Digit3',
    Qt.Key.Key_4: 'Digit4',
    Qt.Key.Key_5: 'Digit5',
    Qt.Key.Key_6: 'Digit6',
    Qt.Key.Key_7: 'Digit7',
    Qt.Key.Key_8: 'Digit8',
    Qt.Key.Key_9: 'Digit9',
    Qt.Key.Key_A: 'KeyA',
    Qt.Key.Key_B: 'KeyB',
    Qt.Key.Key_C: 'KeyC',
    Qt.Key.Key_D: 'KeyD',
    Qt.Key.Key_E: 'KeyE',
    Qt.Key.Key_F: 'KeyF',
    Qt.Key.Key_G: 'KeyG',
    Qt.Key.Key_H: 'KeyH',
    Qt.Key.Key_I: 'KeyI',
    Qt.Key.Key_J: 'KeyJ',
    Qt.Key.Key_K: 'KeyK',
    Qt.Key.Key_L: 'KeyL',
    Qt.Key.Key_M: 'KeyM',
    Qt.Key.Key_N: 'KeyN',
    Qt.Key.Key_O: 'KeyO',
    Qt.Key.Key_P: 'KeyP',
    Qt.Key.Key_Q: 'KeyQ',
    Qt.Key.Key_R: 'KeyR',
    Qt.Key.Key_S: 'KeyS',
    Qt.Key.Key_T: 'KeyT',
    Qt.Key.Key_U: 'KeyU',
    Qt.Key.Key_V: 'KeyV',
    Qt.Key.Key_W: 'KeyW',
    Qt.Key.Key_X: 'KeyX',
    Qt.Key.Key_Y: 'KeyY',
    Qt.Key.Key_Z: 'KeyZ',
}

QTKEY_TO_CODE = {
  Qt.Key.Key_A: "KeyA",
  Qt.Key.Key_S: "KeyS",
  Qt.Key.Key_D: "KeyD",
  Qt.Key.Key_F: "KeyF",
  Qt.Key.Key_H: "KeyH",
  Qt.Key.Key_G: "KeyG",
  Qt.Key.Key_Z: "KeyZ",
  Qt.Key.Key_X: "KeyX",
  Qt.Key.Key_C: "KeyC",
  Qt.Key.Key_V: "KeyV",
  Qt.Key.Key_B: "KeyB",
  Qt.Key.Key_Q: "KeyQ",
  Qt.Key.Key_W: "KeyW",
  Qt.Key.Key_E: "KeyE",
  Qt.Key.Key_R: "KeyR",
  Qt.Key.Key_Y: "KeyY",
  Qt.Key.Key_T: "KeyT",
  Qt.Key.Key_1: "Digit1",
  Qt.Key.Key_2: "Digit2",
  Qt.Key.Key_3: "Digit3",
  Qt.Key.Key_4: "Digit4",
  Qt.Key.Key_5: "Digit6",
  Qt.Key.Key_6: "Digit5",
  Qt.Key.Key_Equal: "Equal",
  Qt.Key.Key_9: "Digit9",
  Qt.Key.Key_7: "Digit7",
  Qt.Key.Key_Minus: "Minus",
  Qt.Key.Key_8: "Digit8",
  Qt.Key.Key_0: "Digit0",
  Qt.Key.Key_BracketRight: "BracketRight",
  Qt.Key.Key_O: "KeyO",
  Qt.Key.Key_U: "KeyU",
  Qt.Key.Key_BracketLeft: "BracketLeft",
  Qt.Key.Key_I: "KeyI",
  Qt.Key.Key_P: "KeyP",
  Qt.Key.Key_Enter: "Enter",
  Qt.Key.Key_L: "KeyL",
  Qt.Key.Key_J: "KeyJ",
  Qt.Key.Key_Apostrophe: "Quote",
  Qt.Key.Key_K: "KeyK",
  Qt.Key.Key_Semicolon: "Semicolon",
  Qt.Key.Key_Backslash: "Backslash",
  Qt.Key.Key_Comma: "Comma",
  Qt.Key.Key_Slash: "Slash",
  Qt.Key.Key_N: "KeyN",
  Qt.Key.Key_M: "KeyM",
  Qt.Key.Key_Period: "Period",
  Qt.Key.Key_Tab: "Tab",
  Qt.Key.Key_Space: "Space",
  Qt.Key.Key_QuoteLeft: "Backquote",
  Qt.Key.Key_Backspace: "Backspace",
  #Qt.Key.Key_0: "NumpadEnter",
  Qt.Key.Key_Escape: "Escape",
  #Qt.Key.Key_0: "MetaRight",
  #Qt.Key.Key_Meta: "MetaLeft",
  Qt.Key.Key_Meta: "MetaLeft",
  Qt.Key.Key_Shift: "ShiftLeft",
  Qt.Key.Key_CapsLock: "CapsLock",
  Qt.Key.Key_Alt: "AltLeft",
  Qt.Key.Key_Control: "ControlLeft",
  #Qt.Key.Key_0: "ShiftRight",
  #Qt.Key.Key_0: "AltRight",
  #Qt.Key.Key_0: "ControlRight",
  Qt.Key.Key_F17: "F17",
  #Qt.Key.Key_0: "NumpadDecimal",
  #Qt.Key.Key_0: "NumpadMultiply",
  #Qt.Key.Key_0: "NumpadAdd",
  #Qt.Key.Key_0: "NumLock",
  #Qt.Key.Key_0: "VolumeUp",
  #Qt.Key.Key_0: "VolumeDown",
  #Qt.Key.Key_0: "VolumeMute",
  #Qt.Key.Key_0: "NumpadDivide",
  #Qt.Key.Key_0: "NumpadEnter",
  #Qt.Key.Key_0: "NumpadSubtract",
  Qt.Key.Key_F18: "F18",
  Qt.Key.Key_F19: "F19",
  #Qt.Key.Key_0: "NumpadEqual",
  #Qt.Key.Key_0: "Numpad0",
  #Qt.Key.Key_0: "Numpad1",
  #Qt.Key.Key_0: "Numpad2",
  #Qt.Key.Key_0: "Numpad3",
  #Qt.Key.Key_0: "Numpad4",
  #Qt.Key.Key_0: "Numpad5",
  #Qt.Key.Key_0: "Numpad6",
  #Qt.Key.Key_0: "Numpad7",
  Qt.Key.Key_F20: "F20",
  #Qt.Key.Key_0: "Numpad8",
  #Qt.Key.Key_0: "Numpad9",
  #Qt.Key.Key_0: "NumpadComma",
  Qt.Key.Key_F5: "F5",
  Qt.Key.Key_F6: "F6",
  Qt.Key.Key_F7: "F7",
  Qt.Key.Key_F3: "F3",
  Qt.Key.Key_F8: "F8",
  Qt.Key.Key_F9: "F9",
  Qt.Key.Key_F11: "F11",
  Qt.Key.Key_F13: "F13",
  Qt.Key.Key_F16: "F16",
  Qt.Key.Key_F14: "F14",
  Qt.Key.Key_F10: "F10",
  #Qt.Key.Key_0: "ContextMenu",
  Qt.Key.Key_F12: "F12",
  Qt.Key.Key_F15: "F15",
  Qt.Key.Key_Help: "Help",
  Qt.Key.Key_Home: "Home",
  Qt.Key.Key_PageUp: "PageUp",
  Qt.Key.Key_Delete: "Delete",
  Qt.Key.Key_F4: "F4",
  Qt.Key.Key_End: "End",
  Qt.Key.Key_F2: "F2",
  Qt.Key.Key_PageDown: "PageDown",
  Qt.Key.Key_F1: "F1",
  Qt.Key.Key_Left: "ArrowLeft",
  Qt.Key.Key_Right: "ArrowRight",
  Qt.Key.Key_Down: "ArrowDown",
  Qt.Key.Key_Up: "ArrowUp",
}

class ImageWidget(QWidget):
  def __init__(self, node: ObservableDict, stream: typing.AsyncIterable[thalamus_pb2.Image], control_queue: IterableQueue, stub: thalamus_pb2_grpc.ThalamusStub, done_future):
    self.node = node
    self.stream = stream
    self.stub = stub
    self.image: typing.Optional[QImage] = None
    self.done_future = done_future
    self.control_queue = control_queue

    x, y, w, h = [100, 100, 400, 400]

    self.view_geometry_updater = None
    def on_view_geometry():
      print(node)
      self.view_geometry_updater = MeteredUpdater(node['view_geometry'], datetime.timedelta(seconds=1), lambda: isdeleted(self))
      
    if 'view_geometry' not in node:
      node.setitem('view_geometry', [100, 100, 400, 400], on_view_geometry)
    else:
      x, y, w, h = node['view_geometry']
      on_view_geometry()

    super().__init__()
    self.setWindowTitle(node['name'])

    node.add_observer(self.on_change, functools.partial(isdeleted, self))

    self.move(x, y)
    self.resize(w, h)

    asyncio.get_event_loop().create_task(self.__stream_task(stream))
    
    self.show()

  def key_event(self, e: QKeyEvent, event_type):
    if e.key() not in QTKEY_TO_CODE:
      return
    event = {
      event_type :{
        'code': QTKEY_TO_CODE[e.key()],
        'type': event_type
      }
    }
    request = thalamus_pb2.NodeRequest(
      node = self.node['name'],
      json = json.dumps(event)
    )
    create_task_with_exc_handling(self.control_queue.put(request))

  def mouse_event(self, e: QMouseEvent, event_type):
    if self.image is None:
      return

    scale = min(self.width()/self.image.width(), self.height()/self.image.height())
    offset = -(self.image.width()*scale - self.width())/2, -(self.image.height()*scale - self.height())/2

    event = {
      event_type :{
        'type': event_type,
        'offsetX': int(float(qt_get_x(e) + offset[0])/self.width()*self.image.width()),
        'offsetY': int(float(qt_get_y(e) + offset[1])/self.height()*self.image.height()),
        'button': qt_get_button_int(e),
        'buttons': qt_get_buttons_int(e)
      }
    }
    request = thalamus_pb2.NodeRequest(
      node = self.node['name'],
      json = json.dumps(event)
    )
    print(request)
    create_task_with_exc_handling(self.control_queue.put(request))

  def keyReleaseEvent(self, a0: QKeyEvent):
    self.key_event(a0, 'keyup')

  def keyPressEvent(self, a0: QKeyEvent):
    self.key_event(a0, 'keydown')

  def mousePressEvent(self, a0: QMouseEvent):
    self.mouse_event(a0, 'mousedown')

  def mouseReleaseEvent(self, a0: QMouseEvent):
    self.mouse_event(a0, 'mouseup')

  def mouseMoveEvent(self, a0: QMouseEvent):
    self.mouse_event(a0, 'mousemove')

  def paintEvent(self, a0):
    super().paintEvent(a0)
    painter = QPainter(self)
    if self.image is not None:
      scale = min(self.width()/self.image.width(), self.height()/self.image.height())
      painter.translate(-(self.image.width()*scale - self.width())/2, -(self.image.height()*scale - self.height())/2)
      painter.scale(scale, scale)
      painter.drawImage(0, 0, self.image)

  async def __stream_task(self, stream: typing.AsyncIterable[thalamus_pb2.Image]):
    response = thalamus_pb2.Image()
    try:
      async for response_piece in stream:
        for i, data in enumerate(response_piece.data):
          if len(response.data) <= i:
            response.data.append(data)
          else:
            response.data[i] += data

        if not response_piece.last:
          continue

        response.width = response_piece.width
        response.height = response_piece.height
        response.format = response_piece.format

        if response.format == thalamus_pb2.Image.Format.Gray:
          format = QImage.Format.Format_Grayscale8
          data = response.data[0]
          if response.width*response.height != len(data):
            data = numpy.array(numpy.frombuffer(data, dtype=numpy.uint8).reshape(response.height,-1)[:,:response.width])
          else:
            data = data
        elif response.format == thalamus_pb2.Image.Format.RGB:
          format = QImage.Format.Format_RGB888
          data = response.data[0]
          if response.width*3*response.height != len(data):
            data = numpy.array(numpy.frombuffer(data, dtype=numpy.uint8).reshape(response.height,-1)[:,:3*response.width])
          else:
            data = data
        elif response.format == thalamus_pb2.Image.Format.YUYV422:
          format = QImage.Format.Format_RGB888
          data = response.data[0]
          if response.width*2*response.height != len(data):
            data = numpy.array(numpy.frombuffer(data, dtype=numpy.uint8).reshape(response.height,-1)[:,:2*response.width])
          else:
            data = numpy.frombuffer(data, dtype=numpy.uint8).reshape(response.height,response.width,-1)[:,:,:2]
          data = cv2.cvtColor(data, cv2.COLOR_YUV2RGB_YUYV)
        elif response.format in (thalamus_pb2.Image.Format.YUVJ420P, thalamus_pb2.Image.Format.YUV420P):
          format = QImage.Format.Format_RGB888
          luminance = response.data[0]
          if response.width*response.height != len(luminance):
            luminance = numpy.array(numpy.frombuffer(luminance, dtype=numpy.uint8).reshape(response.height,-1)[:,:response.width])
          else:
            luminance = numpy.frombuffer(luminance, dtype=numpy.uint8).reshape(response.height,response.width)

          chroma1 = response.data[1]
          if response.width*response.height//4 != len(chroma1):
            chroma1 = numpy.array(numpy.frombuffer(chroma1, dtype=numpy.uint8).reshape(response.height//2,-1)[:,:response.width//2])
          else:
            chroma1 = numpy.frombuffer(chroma1, dtype=numpy.uint8).reshape(response.height//2,response.width//2)

          chroma2 = response.data[2]
          if response.width*response.height//4 != len(chroma2):
            chroma2 = numpy.array(numpy.frombuffer(chroma2, dtype=numpy.uint8).reshape(response.height//2,-1)[:,:response.width//2])
          else:
            chroma2 = numpy.frombuffer(chroma2, dtype=numpy.uint8).reshape(response.height//2,response.width//2)

          chroma_all = numpy.zeros((response.height//2, response.width//2, 2), dtype=numpy.uint8)
          chroma_all[:,:,0] = chroma1
          chroma_all[:,:,1] = chroma2
          #if response.format == thalamus_pb2.Image.Format.YUVJ420P:
          #  luminance = numpy.array(luminance, dtype=numpy.int32)
          #  chroma_all = numpy.zeros((response.height//2, response.width//2, 2), dtype=numpy.int32)
          #  chroma_all[:,:,0] = chroma1
          #  chroma_all[:,:,1] = chroma2

          #  luminance = numpy.array(numpy.where(luminance >= 128, (luminance-128)*(235-128)//(255-128), (luminance-128)*(16-128)//(0-128)), dtype=numpy.uint8)
          #  chroma_all = numpy.array(numpy.where(chroma_all >= 128, (chroma_all-128)*(240-128)//(255-128), (chroma_all-128)*(16-128)//(0-128)), dtype=numpy.uint8)

          data = cv2.cvtColorTwoPlane(luminance, chroma_all, cv2.COLOR_YUV2RGB_NV12)
          #data = luminance

        self.image = QImage(data, response.width, response.height, format)
        self.update()
        response = thalamus_pb2.Image()
    except grpc.aio.AioRpcError as e:
      pass
    except asyncio.CancelledError:
      pass

  def on_change(self, action, key, value):
    if key == 'View':
      if not value:
        self.close()

  def moveEvent(self, a0: QMoveEvent) -> None:
    offset = self.frameGeometry().size() - self.geometry().size()
    position = a0.pos() - QPoint(offset.width(), offset.height())
    position = QPoint(max(0, position.x()), max(0, position.y()))
    if self.view_geometry_updater:
      self.view_geometry_updater[:2] = position.x(), position.y()
    return super().moveEvent(a0)

  def resizeEvent(self, a0: QResizeEvent) -> None:
    if self.view_geometry_updater:
      self.view_geometry_updater[2:] = a0.size().width(), a0.size().height()
    return super().resizeEvent(a0)

  def closeEvent(self, a0: QCloseEvent) -> None:
    self.node['View'] = False
    self.stream.cancel()
    self.done_future.set_result(None)
    return super().closeEvent(a0)

async def main():
  try:
    parser = argparse.ArgumentParser(description='Thalamus Image Viewer')
    parser.add_argument('-a', '--address', default='localhost:50050', help='Thalamus addres, [ip:port]')
    parser.add_argument('-n', '--node', help='Node name')
    parser.add_argument('-f', '--framerate', type=float, default=60.0, help='Max framerate')
    args = parser.parse_args()

    _ = QApplication(sys.argv)

    thread = ThalamusThread(args.address)
    task = await thread.async_start()
    try:

      def on_change(source, action, key, value):
        print(source, action, key, value)

      node: typing.Optional[ObservableDict] = None
      for n in thread.config['nodes']:
        if n['name'] == args.node:
          n.add_recursive_observer(on_change)
          node = n
          break
      assert node is not None

      assert thread.stub is not None
      request = thalamus_pb2.ImageRequest(node=thalamus_pb2.NodeSelector(name=args.node), framerate=args.framerate)
      stream = thread.stub.image(request)

      control_queue = IterableQueue()
      control_stream = thread.stub.node_request_stream(control_queue)
      async def control_consumer():
        try:
          async for v in control_stream:
            print(v)
        except asyncio.CancelledError:
          pass
        except grpc.aio.AioRpcError as e:
          if e.code() not in (grpc.StatusCode.CANCELLED, grpc.StatusCode.UNAVAILABLE):
            raise
      consumer_task = create_task_with_exc_handling(control_consumer())

      done_future = asyncio.get_event_loop().create_future()
      widget = ImageWidget(node, stream, control_queue, thread.stub, done_future)

      while not done_future.done():
        QApplication.processEvents()
        await asyncio.sleep(.016)
      if not done_future.done():
        done_future.set_result(None)

      consumer_task.cancel()
      await consumer_task
    except KeyboardInterrupt:
      pass
    except grpc.aio.AioRpcError as e:
      pass
    finally:
      task.cancel()
  except:
    traceback.print_exc()

if __name__ == '__main__':
  loop = asyncio.get_event_loop()
  loop.run_until_complete(main())
