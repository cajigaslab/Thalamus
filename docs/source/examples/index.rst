Examples
========

These examples are runnable end to end and require **no acquisition hardware**.
They mirror the scripts in the `examples/ <https://github.com/cajigaslab/Thalamus/tree/main/examples>`_
folder of the repository, so you can copy-paste from here or run the scripts
directly.

If you use Thalamus in your work, please cite our paper:
`Thalamus: a real-time, closed-loop platform for synchronized multimodal data
acquisition <https://www.nature.com/articles/s44172-026-00646-z>`_
(Communications Engineering, Nature).

.. contents::
   :local:

Synthesize and analyze a recording without hardware
----------------------------------------------------

A Thalamus recording (a ``.tha`` capture file) is simply a sequence of
length-prefixed protobuf ``StorageRecord`` messages: a leading ``metadata``
record followed by ``analog`` (or ``image``, ``text``, ...) records.  Because the
format is open, you can produce a perfectly valid recording programmatically and
then exercise the full analysis toolchain against it.

This example generates a 5-second recording with two channels -- a 2 Hz sine wave
and a 1 Hz square pulse -- and then reads, exports, hydrates, and plots it.

1. Generate a capture file
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following script writes a file that is indistinguishable from one produced by
a STORAGE2 node.  Each analog record carries 16 ms of data; channels are
concatenated in ``data`` and a ``Span`` maps each slice to a named channel.

.. code-block:: python

   import math, datetime, pathlib
   from thalamus.thalamus_pb2 import StorageRecord, AnalogResponse, Span, Metadata, Pair
   from thalamus.record_writer import write_record

   SAMPLE_RATE, POLL_MS, DURATION_S = 1000, 16, 5
   INTERVAL_NS = int(1e9 / SAMPLE_RATE)
   SAMPLES_PER_POLL = SAMPLE_RATE * POLL_MS // 1000
   total = SAMPLE_RATE * DURATION_S

   out = pathlib.Path(f"synthetic.tha.{datetime.date.today():%Y%m%d}.1")
   with out.open("wb") as stream:
       write_record(stream, StorageRecord(
           node="storage", time=0,
           metadata=Metadata(keyvalues=[Pair(key="Rec", integral=1)])))
       produced, t_ns = 0, 0
       while produced < total:
           count = min(SAMPLES_PER_POLL, total - produced)
           sine, pulse = [], []
           for i in range(count):
               t = (produced + i) / SAMPLE_RATE
               sine.append(math.sin(2 * math.pi * 2.0 * t))
               pulse.append(5.0 if int(t * 2) % 2 == 0 else 0.0)
           produced += count
           t_ns += count * INTERVAL_NS
           write_record(stream, StorageRecord(
               node="wave", time=t_ns,
               analog=AnalogResponse(
                   data=sine + pulse,
                   spans=[Span(begin=0, end=count, name="sine"),
                          Span(begin=count, end=2 * count, name="pulse")],
                   sample_intervals=[INTERVAL_NS, INTERVAL_NS], time=t_ns)))
   print("wrote", out)

The full script is available as ``examples/synthetic_recording.py``:

.. code-block::

   python examples/synthetic_recording.py -o demo.tha

2. Inspect the records
^^^^^^^^^^^^^^^^^^^^^^^

You can iterate over the records directly with ``thalamus.record_reader2``:

.. code-block::

   python -m thalamus.record_reader2 demo.tha

3. Export to CSV or Parquet
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``thalamus.dataframe`` module turns a node's channels into a tabular file:

.. code-block::

   python -m thalamus.dataframe -n wave -i demo.tha -f csv -o demo.csv

The output is indexed by ``counter`` (the sample timestamp in nanoseconds) with one
column per channel::

   counter,sine,pulse
   1000000,0.0,5.0
   2000000,0.012566039883352607,5.0
   ...

4. Hydrate to HDF5
^^^^^^^^^^^^^^^^^^

To produce a single self-describing HDF5 file for downstream analysis, hydrate the
capture:

.. code-block::

   python -m thalamus.hydrate demo.tha

This writes ``demo.tha.h5`` containing, for each channel, a ``data`` dataset of
samples and a ``received`` dataset of timing information (see :doc:`../quickstart`
for the layout).

5. Plot the channels
^^^^^^^^^^^^^^^^^^^^^

The ``examples/analyze_recording.py`` script reconstructs the per-channel time
series straight from the ``.tha`` file and saves a plot:

.. code-block::

   python examples/analyze_recording.py demo.tha -n wave -o analysis.png

.. image:: synthetic_analysis.png
   :width: 100%
   :alt: Sine and square-pulse channels from the synthetic recording

From here you can apply any analysis you like (NumPy, SciPy, pandas, MATLAB).  For
worked analyses on real recordings -- including the figures from our paper -- see
the `SimpleUseCase <https://github.com/cajigaslab/Thalamus/tree/main/SimpleUseCase>`_
folder.
