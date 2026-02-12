import re
import enum
import collections

import pandas
import numpy

from pprint import pformat

class DataFrameBuilder:
  class Type(enum.Enum):
    Analog = enum.auto()
    Text = enum.auto()
    Invalid = enum.auto()

  def __init__(self, node, data_type: 'DataFrameBuilder.Type', channel_pattern: re.Pattern | str | None = None):
    self.node = node
    self.channel_pattern = re.compile(channel_pattern) if isinstance(channel_pattern, str) else channel_pattern
    self.ref_channel = None
    self.ref_interval = None
    self.data = collections.defaultdict(list)
    self.sample_intervals = {}
    self.counts = []
    self.times = []
    self.__type = data_type
    self.text_time = []
    self.text = []

  def build(self):
    if self.__type == DataFrameBuilder.Type.Analog:
      if not self.counts:
        return pandas.DataFrame({'counter': []})
      
      interpolated_times = numpy.interp(numpy.arange(0, self.counts[-1]+1), self.counts, self.times).astype(int)

      self.data['counter'] = interpolated_times
      #print(self.counts)
      #print(self.times)
      #pprint({k: len(v) for k, v in self.data.items()})
      df = pandas.DataFrame(self.data)
      df = df.set_index('counter')
      return df
    elif self.__type == DataFrameBuilder.Type.Text:
      if not self.text:
        return pandas.DataFrame({'counter': []})
      
      df = pandas.DataFrame({
        'counter': self.text_time,
        'text': pandas.Series(self.text, dtype='string')
      })
      df = df.set_index('counter')
      return df
    else:
      return pandas.DataFrame({'counter': []})

  def update(self, record):
    if record.node != self.node:
      return False

    body = record.WhichOneof('body')
    if body == 'analog':
      return self.__analog(record)
    elif body == 'text':
      return self.__text(record)
    return False

  def __text(self, record):
    if self.__type != DataFrameBuilder.Type.Text:
      return False

    self.text.append(record.text.text)
    self.text_time.append(record.text.time)

  def __analog(self, record):
    if self.__type != DataFrameBuilder.Type.Analog:
      return False

    analog = record.analog
    for sample_interval, span in zip(analog.sample_intervals, analog.spans):
      if self.channel_pattern is not None and not self.channel_pattern.match(span.name):
        continue

      if self.ref_interval is None:
        self.ref_interval = sample_interval

      self.sample_intervals[span.name] = sample_interval
      if self.ref_interval != sample_interval:
        formatted = pformat(self.sample_intervals)
        raise ValueError(f'All Channels must have the same sample interval.  Expected {self.ref_interval}'
                         f'Filter out channels with the channel_pattern parameter.  Got:\n{formatted}')

      if analog.is_int_data:
        data = analog.int_data[span.begin:span.end]
      elif analog.is_ulong_data:
        data = analog.ulong_data[span.begin:span.end]
      else:
        data = analog.data[span.begin:span.end]
      self.data[span.name].extend(data)

      if self.ref_channel is None:
        self.ref_channel = span.name

      if span.name == self.ref_channel:
        if not self.counts:
          self.counts.append(0)
          self.times.append(analog.time - (len(data)-1)*sample_interval)
          if len(data) == 1:
            continue
          last = -1
        else:
          last = self.counts[-1]
        self.counts.append(last + len(data))
        self.times.append(analog.time)

    return True
