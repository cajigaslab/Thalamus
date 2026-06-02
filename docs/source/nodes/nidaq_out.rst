NIDAQ_OUT
=========

The ``NIDAQ_OUT (NIDAQMX)`` node is a consumer that writes an analog stream from
Thalamus out to a National Instruments DAQ.  It is the counterpart to the
:doc:`NIDAQ <nidaq>` input node and is commonly used to drive external hardware --
for example writing computed gaze coordinates out as voltages, or generating
stimulation waveforms.

Properties
----------

* **Source**: The node whose data is written to the DAQ.
* **Channel**: A list of DAQ output channels (and channel ranges) to write to.
* **Digital**: Write to digital output lines instead of analog output channels.
* **Running**: Begin writing to the DAQ.

The output sample rate follows from the source node's data; the timing parameters
(``Sample Rate``, ``Poll Interval``, ``Channel Type``) configured on the
:doc:`NIDAQ <nidaq>` input node do not apply here.
