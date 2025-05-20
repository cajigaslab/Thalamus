import os
import re
import enum
import struct
import typing
import pathlib
import argparse
import datetime

from .thalamus_pb2 import StorageRecord
from .record_reader import *
from .record_writer import *

import google.protobuf.message

class Body(enum.Enum):
  analog = 'analog'
  xsens = 'xsens'
  event = 'event'
  image = 'image'
  text = 'text'
  compressed = 'compressed'
  metadata = 'metadata'

class Args(typing.NamedTuple):
  exclude: bool
  type: Body | None
  name: re.Pattern | None
  start: datetime.timedelta | None
  stop: datetime.timedelta | None
  input: pathlib.Path | None
  output: pathlib.Path | None

def duration(text: str) -> datetime.timedelta:
  if text.endswith('ms'):
    return datetime.timedelta(milliseconds=int(text[:-2]))
  elif text.endswith('s'):
    return datetime.timedelta(seconds=float(text[:-1]))
  elif text.endswith('m'):
    return datetime.timedelta(minutes=float(text[:-1]))
  elif text.endswith('h'):
    return datetime.timedelta(hours=float(text[:-1]))
  raise RuntimeError(f'Failed to parse {text} as time')

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

def format_bytes(count: int):
  if count >= 1e9:
    return f'{count/1e9:.3f}GB'
  elif count >= 1e6:
    return f'{count/1e6:.3f}MB'
  elif count >= 1e3:
    return f'{count/1e3:.3f}KB'
  else:
    return f'{count}B'

def main():
  parser = argparse.ArgumentParser(description='Thalamus stream editor')
  parser.add_argument('-e', '--exclude', action='store_true', help='Exclude matching nodes')
  parser.add_argument('-t', '--type', type=Body, help='Type of nodes to match')
  parser.add_argument('-n', '--name', type=re.compile, help='Name of nodes to match')
  parser.add_argument('-s', '--start', type=duration, help='Time to start matching records')
  parser.add_argument('-z', '--stop', type=duration, help='Time to stop matching records')
  parser.add_argument('-i', '--input', type=pathlib.Path, help='Input file')
  parser.add_argument('-o', '--output', type=pathlib.Path, help='Output file')
  args = typing.cast(Args, parser.parse_args())

  output = None

  if args.output is not None:
    if args.output.exists():
      raise RuntimeError('output file exists')
    #if args.input.resolve(True) == args.output.resolve(True):
    #  raise RuntimeError('input and output files must point to different files')

  if args.output is None:
    output = os.fdopen(sys.stdout.fileno(), 'wb', closefd=False)
  else:
    output = open(args.output, 'wb')

  if args.input is None:
    input = os.fdopen(sys.stdin.fileno(), 'rb', closefd=False)
  else:
    input = open(args.input, 'rb')

  conditions = []
  if args.name is not None:
    conditions.append(lambda record, time, name=args.name: name.match(record.node))
  if args.type is not None:
    conditions.append(lambda record, time, type=args.type.value: type == record.WhichOneof('body'))
  if args.start is not None:
    conditions.append(lambda record, time: time >= args.start)
  if args.stop is not None:
    conditions.append(lambda record, time: time <= args.stop)

  start_time = None
  with input, RecordReader(input, False, False) as record_reader, output:
    timer = Timer(1)
    timer.add_callback(lambda: print(f'{100*record_reader.progress():.2f}%\t{format_bytes(record_reader.current_position)}', file=sys.stderr))
    for record in record_reader:
      timer.poll()
      if start_time is None:
        start_time = record.time
      current_time = datetime.timedelta(microseconds=(record.time-start_time)/1e3)
      matched = True
      for c in conditions:
        matched = c(record, current_time)
        if not matched:
          break
      if args.exclude:
        if not matched:
          write_record(output, record)
      else:
        if matched:
          write_record(output, record)
      if not args.exclude and args.stop is not None and args.stop < current_time:
        break


if __name__ == '__main__':
  main()
