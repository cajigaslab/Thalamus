import re
import io
import sys
import time
import zlib
import json
import shutil
import typing
import struct
import pickle
import pathlib
import argparse
import threading
import traceback
import itertools
import contextlib
import subprocess
import collections
from multiprocessing.pool import ThreadPool

import yaml
import numpy
import scipy.io
import pkg_resources

from thalamus.thalamus_pb2 import StorageRecord, Image, Compressed
import google.protobuf.message

EXECUTABLE_EXTENSION = '.exe' if sys.platform == 'win32' else ''

LONG = ">Q"
LONG_SIZE = struct.calcsize(LONG)
MAX_SIZE = 40e6

class PendingMessage(typing.NamedTuple):
  size: int
  type: int
  stream: int

class ZQueue:
  def __init__(self, stream: int):
    self.inflater = zlib.decompressobj()
    self.messages: collections.deque[Compressed] = collections.deque()
    self.pending_messages: collections.deque[PendingMessage] = collections.deque()
    self.lock = threading.Lock()
    self.done = False
    self.working = False
    self.stream_id = stream
    self.output_buffer = b''
    self.output_messages: collections.deque[StorageRecord] = collections.deque()
    self.gg = 0

  def push(self, message: Compressed):
    with self.lock:
      self.messages.append(message)

  def pull(self):
    with self.lock:
      while not self.output_messages and not self.done:
        self.lock.release()
        time.sleep(1)
        self.lock.acquire()
      if self.done:
        return None
      return self.output_messages.popleft()

  def work(self):
    try:
      while True:
        buffer = b''
        with self.lock:
          if self.working or self.done:
            return
          messages = self.messages
          self.messages = collections.deque()
          self.working = bool(messages)

        for m in messages:
          buffer += m.data
          if m.type == Compressed.Type.NONE:
            continue
          self.pending_messages.append(PendingMessage(m.size, m.type, m.stream))

        if not buffer:
          with self.lock:
            self.working = False
            return

        new_output = self.inflater.decompress(buffer)
        self.output_buffer += new_output
        new_output_messages = []
        while self.pending_messages:
          pending = self.pending_messages[0]
          if pending.size > len(self.output_buffer):
            break

          self.pending_messages.popleft()

          try:
            message = StorageRecord()
            message.ParseFromString(self.output_buffer[:pending.size])
            new_output_messages.append(message)
            self.output_buffer = self.output_buffer[pending.size:]
            self.gg += 1
          except google.protobuf.message.DecodeError:
            traceback.print_exc()
            with self.lock:
              self.done = True
            break
        with self.lock:
          self.output_messages.extend(new_output_messages)
          self.working = False
    except:
      traceback.print_exc()

#class VideoMuxer:
#  def __init__(self, output_file: pathlib.Path):
#    self.process = subprocess.Popen(f'ffmpeg -y -i pipe: -c:v copy {output_file}')
#    
#  def push(self, message: Image)

class RecordReader:
  def __init__(self, file_arg: typing.Union[str, pathlib.Path, io.BufferedReader], decompress=True, mux=True):
    self.filename: typing.Optional[pathlib.Path]
    self.reader: typing.Optional[io.BufferedReader]
    self.size = 0
    self.current_position = 0
    self.decompress = decompress
    self.mux = mux
    self.muxers: typing.Dict[str, subprocess.Popen] = {}
    self.running = False
    self.pool = ThreadPool()
    self.thread: typing.Optional[threading.Thread] = None
    self.condition = threading.Condition()
    self.lock = threading.Lock()
    self.z_queues = {}
    self.image_nodes: typing.List[str] = []
    self.records: collections.deque[typing.Tuple[int, typing.Union[StorageRecord, PendingMessage, None]]] = collections.deque()
    if isinstance(file_arg, (str, pathlib.Path)):
      self.filename = pathlib.Path(file_arg)
      self.reader = None
    else:
      self.filename = None
      self.reader = file_arg
      self.measure()

  def is_running(self):
    with self.lock:
      return self.running

  def reader_thread(self):
    muxers: typing.Dict[str, subprocess.Popen] = {}
    assert self.reader is not None
    try:
      #print('reader', self.reader.tell(), self.size)
      position = 0
      while self.is_running():
        #print('reader', 'r')
        record, read_size = self.__read_record() or (None, 0)
        if self.reader.seekable():
          position = self.reader.tell()
        else:
          position += read_size
        #print('reader', record)
        if record is None:
          with self.lock:
            self.records.append((self.size, None))
          return
        
        body_type = record.WhichOneof('body')
        #print(record.node, body_type)
        if body_type == 'compressed':
          if not self.decompress:
            with self.lock:
              self.records.append((position, record))
              continue

          compressed = record.compressed
          with self.lock:
            if compressed.stream not in self.z_queues:
              self.z_queues[compressed.stream] = ZQueue(compressed.stream)
            z_queue = self.z_queues[compressed.stream]
            z_queue.push(compressed)
            self.pool.apply_async(z_queue.work)

          if compressed.type == Compressed.Type.NONE:
            continue

          with self.lock:
            self.records.append((position, PendingMessage(compressed.size, compressed.type, compressed.stream)))
        elif body_type == 'image':
          image = record.image
          if image.format in (Image.Format.MPEG1, Image.Format.MPEG4, Image.Format.Gray, Image.Format.RGB) and self.mux:
            if record.node not in muxers:
              self.image_nodes.append(record.node)
              output_file = f'{self.filename}.{record.node}.avi'
              if image.format in (Image.Format.MPEG1, Image.Format.MPEG4,):
                muxers[record.node] = subprocess.Popen(f'ffmpeg -y -i pipe: -c:v copy "{output_file}"', stdin=subprocess.PIPE, shell=True)
              elif image.format in (Image.Format.Gray, Image.Format.RGB):
                framerates = [
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
                framerate = min(framerates, key=lambda a: abs(a[0] - 1e9/(image.frame_interval or 16e6)))
                if image.format == Image.Format.Gray:
                  format = 'gray'
                #elif image.format == Image.Format.RGB:
                else:
                  format = 'rgb24'
                command = (f'ffmpeg -y -f rawvideo -r {framerate[1]} -pixel_format {format} -video_size {image.width}x{image.height} '
                           f'-i pipe: -qscale:v 2 -b:v 100M "{output_file}"')
                print('COMMAND', command)
                muxers[record.node] = subprocess.Popen(command, stdin=subprocess.PIPE, shell=True)
            muxer = muxers[record.node]
            assert muxer.stdin is not None
            if len(image.data) > 0:
              muxer.stdin.write(image.data[0])
            if image.width > 0:
              with self.lock:
                self.records.append((position, record))
          else:
            with self.lock:
              self.records.append((position, record))
        else:
          with self.lock:
            self.records.append((position, record))
    except:
      traceback.print_exc()
    finally:
      for k, v in muxers.items():
        assert v.stdin is not None
        v.stdin.close()
      for k, v in muxers.items():
        v.wait()

  def get_record(self) -> typing.Optional[StorageRecord]:
    #print('get_record')
    with self.lock:
      while not self.records:
        self.lock.release()
        #print('get_record', 'sleep')
        time.sleep(1)
        self.lock.acquire()
      position, record = self.records.popleft()
      if isinstance(record, PendingMessage):
        z_queue = self.z_queues[record.stream]
        self.lock.release()
        record = z_queue.pull()
        self.lock.acquire()
      self.current_position = position
      return record
    
  def progress(self):
    return self.current_position/self.size
  
  def read_progress(self):
    assert self.reader is not None
    return self.reader.tell()/self.size

  def start(self):
    with self.lock:
      self.running = True
    self.pool.__enter__()
    self.pool.apply_async(self.reader_thread)
    #print('start')

  def stop(self, type = None, value = None, tb = None):
    with self.lock:
      self.running = False
    self.pool.__exit__(type, value, tb)

  def __enter__(self):
    if self.reader is None:
      assert self.filename is not None
      self.reader = open(self.filename, 'rb')
      self.filename = pathlib.Path(self.reader.name)
      self.measure()
    self.start()
    return self
  
  def __exit__(self, type, value, tb):
    self.stop(type, value, tb)
  
  def measure(self):
    assert self.reader is not None
    if not self.reader.seekable():
      self.size = sys.maxsize
      return

    self.reader.seek(0, 2)
    self.size = self.reader.tell()
    self.reader.seek(0, 0)

  def __read_record(self) -> typing.Optional[typing.Tuple[StorageRecord, int]]:
    assert self.reader is not None
    data = self.reader.read(LONG_SIZE)
    if not data:
      return

    size, = struct.unpack(LONG, data)
    if size > MAX_SIZE:
      return
    #if size > 800*600:
    #  self.reader.seek(size, 1)
    #  return self.__read_record()

    data = self.reader.read(size)
    message = StorageRecord()

    try:
      message.ParseFromString(data)
      return message, len(data) + LONG_SIZE
    except google.protobuf.message.DecodeError:
      return
  
  def __iter__(self) -> 'RecordReader':
    return self
  
  def __next__(self) -> StorageRecord:
    record = self.get_record()
    if record is None:
      raise StopIteration()
    return record

class Timer:
  def __init__(self, seconds: float):
    self.seconds = seconds
    self.callbacks: typing.List[typing.Callable[[], None]] = []
    self.last_time = 0.0
    self.reset()

  def reset(self):
    self.last_time = time.perf_counter()

  def add_callback(self, callback: typing.Callable[[], None]):
    self.callbacks.append(callback)

  def poll(self):
    now = time.perf_counter()
    if now - self.last_time >= self.seconds:
      for c in self.callbacks:
        c()
      self.last_time = now

def read_record(stream) -> typing.Optional[StorageRecord]:
  data = stream.read(LONG_SIZE)
  if not data:
    return

  size, = struct.unpack(LONG, data)
  if size > MAX_SIZE:
    return

  data = stream.read(size)
  message = StorageRecord()

  try:
    message.ParseFromString(data)
    return message
  except google.protobuf.message.DecodeError:
    return

def is_capturefile(f: pathlib.Path):
  if not f.is_file():
    return False
  with open(f, 'rb') as stream:
    return read_record(stream) is not None
