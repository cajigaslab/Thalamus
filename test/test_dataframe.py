import os
import tempfile
import unittest

import numpy
import pandas

from thalamus.record_reader2 import RecordReader
from thalamus.dataframe import DataFrameBuilder
from thalamus.thalamus_pb2 import StorageRecord, AnalogResponse, Text, Span
from thalamus.record_writer import write_record

class DataFrameTest(unittest.TestCase):
  def test_analog(self):
    with tempfile.TemporaryFile() as temp_file:
      record = StorageRecord(
        node='This',
        time=2,
        analog=AnalogResponse(
          data = [1, 2, 3, 4, 10, 11],
          spans=[
            Span(name='one', begin=0, end=2), Span(name='two', begin=2, end=4), Span(name='three', begin=10, end=11)
          ],
          sample_intervals=[2, 2, 2],
          time=2
        )
      )
      write_record(temp_file, record)
      record.time += 4
      record.analog.time += 4
      record.analog.data[:] = [5, 6, 7, 8]
      write_record(temp_file, record)
      record.node='That'
      write_record(temp_file, record)
      write_record(temp_file, StorageRecord(node='This'))
      write_record(temp_file, StorageRecord(node='This',text=Text()))

      temp_file.seek(0, os.SEEK_SET)

      builder = DataFrameBuilder('This', DataFrameBuilder.Type.Analog, 'one|two')
      with RecordReader(temp_file) as reader:
        for record in reader:
          builder.update(record)

      df = builder.build()
      print(df)

      numpy.testing.assert_array_equal(df.index.to_numpy(), numpy.array([0, 2, 4, 6]))
      numpy.testing.assert_array_equal(df.one.to_numpy(), numpy.array([1, 2, 5, 6]))
      numpy.testing.assert_array_equal(df.two.to_numpy(), numpy.array([3, 4, 7, 8]))

      self.assertTrue(numpy.issubdtype(df.index.to_numpy().dtype, numpy.integer))
      self.assertTrue(numpy.issubdtype(df.one.to_numpy().dtype, numpy.floating))
      self.assertTrue(numpy.issubdtype(df.two.to_numpy().dtype, numpy.floating))

  def test_text(self):
    with tempfile.TemporaryFile() as temp_file:
      record = StorageRecord(
        node='This',
        time=2,
        text=Text(
          text='One',
          time=2,
        )
      )
      write_record(temp_file, record)
      record.time += 4
      record.text.time += 4
      record.text.text = 'Two'
      write_record(temp_file, record)
      record.node='That'
      write_record(temp_file, record)
      write_record(temp_file, StorageRecord(node='This'))
      write_record(temp_file, StorageRecord(node='This',analog=AnalogResponse()))

      temp_file.seek(0, os.SEEK_SET)

      builder = DataFrameBuilder('This', DataFrameBuilder.Type.Text)
      with RecordReader(temp_file) as reader:
        for record in reader:
          builder.update(record)

      df = builder.build()
      print(df)

      numpy.testing.assert_array_equal(df.index.to_numpy(), numpy.array([2, 6]))
      numpy.testing.assert_array_equal(df.text.to_list(), ['One', 'Two'])

      self.assertTrue(numpy.issubdtype(df.index.to_numpy().dtype, numpy.integer))
      self.assertTrue(pandas.api.types.is_string_dtype(df.text))

  def test_empty(self):
    builder = DataFrameBuilder('This', DataFrameBuilder.Type.Analog)
    df = builder.build()
    self.assertEqual(df.shape[0], 0)

    builder = DataFrameBuilder('This', DataFrameBuilder.Type.Text)
    df = builder.build()
    self.assertEqual(df.shape[0], 0)

    builder = DataFrameBuilder('This', DataFrameBuilder.Type.Invalid)
    df = builder.build()
    self.assertEqual(df.shape[0], 0)

  def test_sample_interval_mismatch(self):
    with tempfile.TemporaryFile() as temp_file:
      record = StorageRecord(
        node='This',
        time=2,
        analog=AnalogResponse(
          data = [1, 2, 3, 4, 10, 11],
          spans=[
            Span(name='one', begin=0, end=2), Span(name='two', begin=2, end=4), Span(name='three', begin=10, end=11)
          ],
          sample_intervals=[2, 3, 2],
          time=2
        )
      )
      write_record(temp_file, record)

      temp_file.seek(0, os.SEEK_SET)

      builder = DataFrameBuilder('This', DataFrameBuilder.Type.Analog, 'one|two')
      with RecordReader(temp_file) as reader:
        for record in reader:
          with self.assertRaises(ValueError):
            builder.update(record)

  def test_analog_int(self):
    with tempfile.TemporaryFile() as temp_file:
      record = StorageRecord(
        node='This',
        time=2,
        analog=AnalogResponse(
          int_data = [1, 2, 3, 4],
          is_int_data=True,
          spans=[
            Span(name='one', begin=0, end=2), Span(name='two', begin=2, end=4)
          ],
          sample_intervals=[2, 2],
          time=2
        )
      )
      write_record(temp_file, record)

      temp_file.seek(0, os.SEEK_SET)

      builder = DataFrameBuilder('This', DataFrameBuilder.Type.Analog)
      with RecordReader(temp_file) as reader:
        for record in reader:
          builder.update(record)

      df = builder.build()
      print(df)

      numpy.testing.assert_array_equal(df.index.to_numpy(), numpy.array([0, 2]))
      numpy.testing.assert_array_equal(df.one.to_numpy(), numpy.array([1, 2]))
      numpy.testing.assert_array_equal(df.two.to_numpy(), numpy.array([3, 4]))

      self.assertTrue(numpy.issubdtype(df.index.to_numpy().dtype, numpy.integer))
      self.assertTrue(numpy.issubdtype(df.one.to_numpy().dtype, numpy.integer))
      self.assertTrue(numpy.issubdtype(df.two.to_numpy().dtype, numpy.integer))

  def test_analog_ulong(self):
    with tempfile.TemporaryFile() as temp_file:
      record = StorageRecord(
        node='This',
        time=2,
        analog=AnalogResponse(
          ulong_data = [1, 2, 3, 4],
          is_ulong_data=True,
          spans=[
            Span(name='one', begin=0, end=2), Span(name='two', begin=2, end=4)
          ],
          sample_intervals=[2, 2],
          time=2
        )
      )
      write_record(temp_file, record)

      temp_file.seek(0, os.SEEK_SET)

      builder = DataFrameBuilder('This', DataFrameBuilder.Type.Analog)
      with RecordReader(temp_file) as reader:
        for record in reader:
          builder.update(record)

      df = builder.build()
      print(df)

      numpy.testing.assert_array_equal(df.index.to_numpy(), numpy.array([0, 2]))
      numpy.testing.assert_array_equal(df.one.to_numpy(), numpy.array([1, 2]))
      numpy.testing.assert_array_equal(df.two.to_numpy(), numpy.array([3, 4]))

      self.assertTrue(numpy.issubdtype(df.index.to_numpy().dtype, numpy.integer))
      self.assertTrue(numpy.issubdtype(df.one.to_numpy().dtype, numpy.integer))
      self.assertTrue(numpy.issubdtype(df.two.to_numpy().dtype, numpy.integer))

  def test_analog_single_sample(self):
    with tempfile.TemporaryFile() as temp_file:
      record = StorageRecord(
        node='This',
        time=2,
        analog=AnalogResponse(
          data = [1, 2],
          spans=[
            Span(name='one', begin=0, end=1), Span(name='two', begin=1, end=2)
          ],
          sample_intervals=[2, 2],
          time=2
        )
      )
      write_record(temp_file, record)
      record.time += 2
      record.analog.time += 2
      record.analog.data[:] = [3, 4]
      write_record(temp_file, record)

      temp_file.seek(0, os.SEEK_SET)

      builder = DataFrameBuilder('This', DataFrameBuilder.Type.Analog)
      with RecordReader(temp_file) as reader:
        for record in reader:
          builder.update(record)

      df = builder.build()
      print(df)

      numpy.testing.assert_array_equal(df.index.to_numpy(), numpy.array([2, 4]))
      numpy.testing.assert_array_equal(df.one.to_numpy(), numpy.array([1, 3]))
      numpy.testing.assert_array_equal(df.two.to_numpy(), numpy.array([2, 4]))
