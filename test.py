import json
import typing
import pprint
import numbers
import sqlite3
import pathlib
import argparse
import subprocess
import collections

import rclpy.serialization

import std_msgs.msg
import sensor_msgs.msg
import comedi_nodes.msg
import oculomatic_msgs.msg
import experiment_coordinator2.msg
import task_controller_interfaces.msg
import rcl_interfaces.msg

import yaml
import numpy
import scipy.io

TYPES = {
  'comedi_nodes/msg/DigWordChunk': comedi_nodes.msg.DigWordChunk, 
  'std_msgs/msg/Empty': std_msgs.msg.Empty, 
  'oculomatic_msgs/msg/Gaze': oculomatic_msgs.msg.Gaze, 
  'experiment_coordinator2/msg/RecordingReport': experiment_coordinator2.msg.RecordingReport, 
  'sensor_msgs/msg/CameraInfo': sensor_msgs.msg.CameraInfo, 
  'task_controller_interfaces/msg/BehavState': task_controller_interfaces.msg.BehavState, 
  'task_controller_interfaces/msg/TrialSummary': task_controller_interfaces.msg.TrialSummary, 
  'sensor_msgs/msg/CompressedImage': sensor_msgs.msg.CompressedImage, 
  'std_msgs/msg/Header': std_msgs.msg.Header, 
  'experiment_coordinator2/msg/RewardDeliveryCmd': experiment_coordinator2.msg.RewardDeliveryCmd, 
  'rcl_interfaces/msg/Log': rcl_interfaces.msg.Log, 
  'sensor_msgs/msg/TimeReference': sensor_msgs.msg.TimeReference
}

class ArrayBuilder():
  def __init__(self, dtype):
    self.data = numpy.zeros((1,), dtype=dtype)
    self.position = 0

  def append(self, to_add):
    if isinstance(to_add, numbers.Number):
      to_add = [to_add]
    while self.position + len(to_add) > len(self.data):
      new_data = numpy.zeros((2*len(self.data),), dtype=self.data.dtype)
      if self.position > 0:
        new_data[:self.position] = self.data
      self.data = new_data

    new_position = self.position + len(to_add)
    self.data[self.position:new_position] = to_add
    self.position = new_position

  def build(self):
    return self.data[:self.position]

  def __str__(self):
    return json.dumps({'position': self.position, 'size': list(self.data.shape)})

  def __repr__(self):
    return json.dumps({'position': self.position, 'size': list(self.data.shape)})

def build_arrays(_dict):
  result = {}
  for k, v in _dict.items():
    if isinstance(v, ArrayBuilder):
      result[k] = v.build()
    else:
      result[k] = v
  return result

class Gaze(typing.NamedTuple):
  header_stamp_nanosec: ArrayBuilder
  header_stamp_sec: ArrayBuilder
  width: ArrayBuilder
  height: ArrayBuilder
  step: ArrayBuilder
  x: ArrayBuilder
  y: ArrayBuilder
  i: ArrayBuilder
  og_width: ArrayBuilder
  og_height: ArrayBuilder
  encoding: typing.List[str]

  def digest(self, timestamp: int, message: oculomatic_msgs.msg.Gaze):
    self.header_stamp_nanosec.append(message.header.stamp.nanosec)
    self.header_stamp_sec.append(message.header.stamp.sec)
    self.width.append(message.image.width)
    self.height.append(message.image.height)
    self.step.append(message.image.step)
    self.x.append(message.x)
    self.y.append(message.y)
    self.i.append(message.i)
    self.og_width.append(message.og_width)
    self.og_height.append(message.og_height)
    self.encoding.append(message.image.encoding)

  @staticmethod
  def make():
    return Gaze(
      header_stamp_nanosec=ArrayBuilder(numpy.uint32),
      header_stamp_sec=ArrayBuilder(numpy.int32),
      width=ArrayBuilder(numpy.int32),
      height=ArrayBuilder(numpy.int32),
      step=ArrayBuilder(numpy.int32),
      x=ArrayBuilder(numpy.float32),
      y=ArrayBuilder(numpy.float32),
      i=ArrayBuilder(numpy.int32),
      og_width=ArrayBuilder(numpy.int32),
      og_height=ArrayBuilder(numpy.int32),
      encoding=[])

class DigWordChunk(typing.NamedTuple):
  timestamp_ns: ArrayBuilder
  header_stamp_nanosec: ArrayBuilder
  header_stamp_sec: ArrayBuilder
  word_chunks: ArrayBuilder
  chunk_sizes: ArrayBuilder

  def digest(self, timestamp: int, message: comedi_nodes.msg.DigWordChunk):
    self.timestamp_ns.append(timestamp)
    self.header_stamp_nanosec.append(message.header.stamp.nanosec)
    self.header_stamp_sec.append(message.header.stamp.sec)
    self.word_chunks.append(message.word_chunk)
    self.chunk_sizes.append(len(message.word_chunk))

  @staticmethod
  def make():
    return DigWordChunk(
      timestamp_ns=ArrayBuilder(numpy.int64),
      header_stamp_nanosec=ArrayBuilder(numpy.uint32),
      header_stamp_sec=ArrayBuilder(numpy.int32),
      word_chunks=ArrayBuilder(numpy.uint32),
      chunk_sizes=ArrayBuilder(numpy.uint32))

class RecordingReport(typing.NamedTuple):
  topic_time_stamp: ArrayBuilder
  telap_sec: ArrayBuilder
  source: typing.List[str]
  recpath: typing.List[str]

  def digest(self, timestamp: int, message: experiment_coordinator2.msg.RecordingReport):
    self.topic_time_stamp.append(timestamp)
    self.telap_sec.append(message.telap_sec)
    self.source.append(message.source)
    self.recpath.append(message.recpath)

  @staticmethod
  def make():
    return RecordingReport(
      topic_time_stamp=ArrayBuilder(numpy.int64),
      telap_sec=ArrayBuilder(numpy.float64),
      source=[],
      recpath=[])

class CameraInfo(typing.NamedTuple):
  header_stamp_nanosec: ArrayBuilder
  header_stamp_sec: ArrayBuilder
  height: ArrayBuilder
  width: ArrayBuilder
  d: ArrayBuilder
  k: ArrayBuilder
  r: ArrayBuilder
  p: ArrayBuilder
  binning_x: ArrayBuilder
  binning_y: ArrayBuilder
  roi_x_offset: ArrayBuilder
  roi_y_offset: ArrayBuilder
  roi_width: ArrayBuilder
  roi_height: ArrayBuilder
  roi_do_rectify: ArrayBuilder
  header_frame_id: typing.List[str]
  distortion_model: typing.List[str]

  def digest(self, timestamp: int, message: sensor_msgs.msg.CameraInfo):
    self.header_stamp_nanosec.append(message.header.stamp.nanosec)
    self.header_stamp_sec.append(message.header.stamp.sec)
    self.width.append(numpy.uint32)
    self.height.append(numpy.uint32)
    self.d.append(numpy.float64)
    self.k.append(numpy.float64)
    self.r.append(numpy.float64)
    self.p.append(numpy.float64)
    self.binning_x.append(numpy.uint32)
    self.binning_y.append(numpy.uint32)
    self.roi_x_offset.append(numpy.uint32)
    self.roi_y_offset.append(numpy.uint32)
    self.roi_width.append(numpy.uint32)
    self.roi_height.append(numpy.uint32)
    self.roi_do_rectify.append(numpy.int8)
    self.header_frame_id.append(message.header.frame_id)
    self.distortion_model.append(message.distortion_model)

  @staticmethod
  def make():
    return CameraInfo(
      header_stamp_nanosec=ArrayBuilder(numpy.uint32),
      header_stamp_sec=ArrayBuilder(numpy.int32),
      width=ArrayBuilder(numpy.uint32),
      height=ArrayBuilder(numpy.uint32),
      d=ArrayBuilder(numpy.float64),
      k=ArrayBuilder(numpy.float64),
      r=ArrayBuilder(numpy.float64),
      p=ArrayBuilder(numpy.float64),
      binning_x=ArrayBuilder(numpy.uint32),
      binning_y=ArrayBuilder(numpy.uint32),
      roi_x_offset=ArrayBuilder(numpy.uint32),
      roi_y_offset=ArrayBuilder(numpy.uint32),
      roi_width=ArrayBuilder(numpy.uint32),
      roi_height=ArrayBuilder(numpy.uint32),
      roi_do_rectify=ArrayBuilder(numpy.int8),
      header_frame_id=[],
      distortion_model=[])

class Empty(typing.NamedTuple):
  topic_time_stamp: ArrayBuilder

  def digest(self, timestamp: int, message: std_msgs.msg.Empty):
    self.topic_time_stamp.append(timestamp)

  @staticmethod
  def make():
    return Empty(topic_time_stamp=ArrayBuilder(numpy.int64))

class BehavState(typing.NamedTuple):
  header_stamp_nanosec: ArrayBuilder
  header_stamp_sec: ArrayBuilder
  state: typing.List[str]

  def digest(self, timestamp: int, message: task_controller_interfaces.msg.BehavState):
    self.header_stamp_nanosec.append(message.header.stamp.nanosec)
    self.header_stamp_sec.append(message.header.stamp.sec)
    self.state.append(message.state)

  @staticmethod
  def make():
    return BehavState(
      header_stamp_nanosec=ArrayBuilder(numpy.uint32),
      header_stamp_sec=ArrayBuilder(numpy.int32),
      state=[])

class TrialSummary(typing.NamedTuple):
  header_stamp_nanosec: ArrayBuilder
  header_stamp_sec: ArrayBuilder
  used_values: typing.List[str]
  task_config: typing.List[str]
  task_result: typing.List[str]
  behav_result: typing.List[str]

  def digest(self, timestamp: int, message: task_controller_interfaces.msg.TrialSummary):
    self.header_stamp_nanosec.append(message.header.stamp.nanosec)
    self.header_stamp_sec.append(message.header.stamp.sec)
    self.used_values.append(message.used_values)
    self.task_config.append(message.task_config)
    self.task_result.append(message.task_result)
    self.behav_result.append(message.behav_result)

  @staticmethod
  def make():
    return TrialSummary(
      header_stamp_nanosec=ArrayBuilder(numpy.uint32),
      header_stamp_sec=ArrayBuilder(numpy.int32),
      used_values=[],
      task_config=[],
      task_result=[],
      behav_result=[])

class CompressedImage(typing.NamedTuple):
  header_stamp_nanosec: ArrayBuilder
  header_stamp_sec: ArrayBuilder
  header_frame_id: typing.List[str]
  format: typing.List[str]

  def digest(self, timestamp: int, message: sensor_msgs.msg.CompressedImage):
    self.header_stamp_nanosec.append(message.header.stamp.nanosec)
    self.header_stamp_sec.append(message.header.stamp.sec)
    self.header_frame_id.append(message.header.frame_id)
    self.format.append(message.format)

  @staticmethod
  def make():
    return CompressedImage(
      header_stamp_nanosec=ArrayBuilder(numpy.uint32),
      header_stamp_sec=ArrayBuilder(numpy.int32),
      header_frame_id=[],
      format=[])

STORAGE_FACTORIES = {
  'comedi_nodes/msg/DigWordChunk': DigWordChunk, 
  'std_msgs/msg/Empty': Empty, 
  'oculomatic_msgs/msg/Gaze': Gaze, 
  'experiment_coordinator2/msg/RecordingReport': RecordingReport, 
  'sensor_msgs/msg/CameraInfo': CameraInfo, 
  'task_controller_interfaces/msg/BehavState': BehavState, 
  'task_controller_interfaces/msg/TrialSummary': TrialSummary, 
  'sensor_msgs/msg/CompressedImage': CompressedImage, 
  'std_msgs/msg/Header': Header, 
  'experiment_coordinator2/msg/RewardDeliveryCmd': RewardDeliveryCmd, 
  'rcl_interfaces/msg/Log': Log, 
  'sensor_msgs/msg/TimeReference': TimeReference
}

def write_topic(output_path: pathlib.Path, name: str, storage: typing.NamedTuple):
  mats = {}
  for k, v in storage._asdict().items():
    if isinstance(v, ArrayBuilder):
      mats[k] = v.build()
    elif isinstance(v, list):
      filename = name.replace('/', '_')[1:] + f'_{k}.yaml'
      with open(output_path / filename, 'w') as output:
        yaml.dump(v, output)
  filename = name.replace('/', '_')[1:] + '.mat'
  scipy.io.savemat(output_path / filename, mats)

def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('-b', '--bag', type=pathlib.Path)
  parser.add_argument('-o', '--out', type=pathlib.Path, default=pathlib.Path.cwd())
  args = parser.parse_args()
  db_path = args.bag / f'{args.bag.name}_0.db3'
  metadata_path = args.bag / 'metadata.yaml'
  mat_path = args.out / 'mat'
  img_path = args.out / 'img'

  mat_path.mkdir(parents=True,exist_ok=True)
  img_path.mkdir(parents=True,exist_ok=True)

  with open(metadata_path) as f:
    metadata = yaml.safe_load(f)
  
  metadata['rosbag2_bagfile_information']['relative_file_paths'][0] = db_path
  pprint.pprint(metadata)

  #Count the data
  topics = set()
  types = set()
  data_counts = {}
  topic_to_type = {}
  with sqlite3.connect(str(db_path)) as con:
    cur = con.cursor()
    for row in cur.execute('SELECT * FROM topics'):
      print(row)
  #return
  topics = {}

  with sqlite3.connect(str(db_path)) as con:
    cur = con.cursor()
    for row in cur.execute('SELECT messages.id, topics.id, topics.name, topics.type, messages.timestamp, messages.data FROM messages JOIN topics ON messages.topic_id=topics.id'):
      message_id, topic_id, topic_name, type_name, timestamp, data = row
      message_type = TYPES[type_name]
      #topics.add(topic_name)
      #types.add(type_name)
      if message_type is None:
        continue
      message = rclpy.serialization.deserialize_message(data, message_type)

      if topic_name not in topics:
        topics[topic_name] = STORAGE_FACTORIES[topic_name].make()

      storage = topics[topic_name]
      storage.digest(timestamp, message)

  print(topics)
  #Save the data
  for k, v in topics.items():
    write_topic(mat_path, k, v)

if __name__ == '__main__':
  main()
