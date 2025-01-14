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
from .util import MeteredUpdater
from .config import ObservableDict

from . import  thalamus_pb2

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

class ImageWidget(QWidget):
  def __init__(self, node: ObservableDict, stream: typing.AsyncIterable[thalamus_pb2.Image], stub, done_future):
    self.node = node
    self.stream = stream
    self.stub = stub
    self.image: typing.Optional[QImage] = None
    self.done_future = done_future

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

  def key_event(self, e, event_type):
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
    async def request_func():
      response = await self.stub.node_request(request)
      print(response)
    asyncio.get_event_loop().create_task(request_func())

  def keyReleaseEvent(self, a0):
    self.key_event(a0, 'keyup')

  def keyPressEvent(self, a0):
    self.key_event(a0, 'keydown')

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
      if e.code() != grpc.StatusCode.CANCELLED:
        raise
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
    parser.add_argument('-f', '--framerate', default=60, help='Max framerate')
    args = parser.parse_args()

    _ = QApplication(sys.argv)

    thread = ThalamusThread(args.address)
    task = await thread.async_start()
    try:

      def on_change(source, action, key, value):
        print(source, action, key, value)

      node = None
      for n in thread.config['nodes']:
        if n['name'] == args.node:
          n.add_recursive_observer(on_change)
          node = n
          break

      request = thalamus_pb2.ImageRequest(node=thalamus_pb2.NodeSelector(name=args.node), framerate=args.framerate)
      stream = thread.stub.image(request)

      done_future = asyncio.get_event_loop().create_future()
      widget = ImageWidget(node, stream, thread.stub, done_future)

      while not done_future.done():
        QApplication.processEvents()
        await asyncio.sleep(.016)
      if not done_future.done():
        done_future.set_result(None)
    except KeyboardInterrupt:
      pass
    finally:
      task.cancel()
  except:
    traceback.print_exc()

if __name__ == '__main__':
  loop = asyncio.get_event_loop()
  loop.run_until_complete(main())
