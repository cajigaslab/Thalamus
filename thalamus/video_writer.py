import io
import sys
import typing
import pathlib
import threading
import subprocess

from thalamus.thalamus_pb2 import StorageRecord, Image

EXECUTABLE_EXTENSION = '.exe' if sys.platform == 'win32' else ''

FRAMERATES = [
  (24000.0 / 1001, "24000/1001"),
  (24, "24"),
  (25, "25"),
  (30000.0 / 1001, "30000/1001"),
  (30, "30"),
  (50, "50"),
  (60000.0 / 1001, "60000/1001"),
  (60, "60"),
  (15, "15"),
  (5, "5"),
  (10, "10"),
  (12, "12"),
  (15, "15")
]

RAW_FORMATS = {
  Image.Format.Gray: 'gray', 
  Image.Format.Gray16: None, 
  Image.Format.RGB: 'rgb24', 
  Image.Format.RGB16: None,
  Image.Format.YUV420P: None,
  Image.Format.YUVJ420P: None,
  Image.Format.YUYV422: None
}

class VideoWriter:
  def __init__(self, file_arg: typing.Union[io.IOBase, pathlib.Path, str], format: str | None = None):
    self.format = format
    self.process: typing.Optional[subprocess.Popen] = None
    self.thread: typing.Optional[threading.Thread] = None

    if isinstance(file_arg, (str, pathlib.Path)):
      self.filename = pathlib.Path(file_arg)
      self.writer = None
    else:
      self.filename = None
      self.writer = file_arg

  def __reader(self):
    assert self.writer is not None
    assert self.process is not None
    data = b'!'
    while data:
      data = self.process.stdout.read(1024*1024)
      self.writer.write(data)

  def setup(self, image: Image):
    command = ['ffmpeg', '-y']
    if image.format in RAW_FORMATS:
      framerate = min(FRAMERATES, key=lambda a: abs(a[0] - 1e9/(image.frame_interval or 16e6)))
      command += ['-f', 'rawvideo']
      command += [ '-r', framerate[1]]
      command += ['-video_size', f'{image.width}x{image.height}']
      command += ['-pixel_format', RAW_FORMATS[image.format]]
      command += ['-i', 'pipe:', '-qscale:v', '2', '-b:v', '100M']
    else:
      command += ['-i', 'pipe:']
      if self.format == 'mp4':
        command += ['-c', 'copy']

    if self.filename is not None:
      command += [self.filename]
      kwargs = {'stdin': subprocess.PIPE}
    else:
      command += ['-f', self.format, 'pipe:']
      kwargs = {'stdin': subprocess.PIPE, 'stdout': subprocess.PIPE}

    self.process = subprocess.Popen(command, **kwargs)

    if self.filename is None:
      self.thread = threading.Thread(target=self.__reader)
      self.thread.start()

  def write(self, image: Image):
    if self.process is None:
      self.setup(image)

    for data in image.data:
      self.process.stdin.write(data)

  def __enter__(self):
    return self
  
  def __exit__(self, type, value, tb):
    if self.process is not None:
      self.process.stdin.close()
      self.process.wait()
    if self.thread is not None:
      self.thread.join()

class MultiVideoWriter:
  def __init__(self, pattern: str = '%s.mp4'):
    self.pattern = pattern
    self.writers: typing.Dict[str, VideoWriter] = {}

  def write(self, record: StorageRecord):
    body = record.WhichOneof('body')
    if body != 'image':
      return False
    
    node = record.node
    if node not in self.writers:
      self.writers[node] = VideoWriter(self.pattern % (node,)).__enter__()
    self.writers[node].write(record.image)
    return True

  def __enter__(self):
    return self
  
  def __exit__(self, type, value, tb):
    for writer in self.writers.values():
      writer.__exit__(type, value, tb)