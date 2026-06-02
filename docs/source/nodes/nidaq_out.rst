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
* **Channel Type**: Output as voltage or current.
* **Sample Rate**: The output sample rate.
* **Poll Interval**: How often, in milliseconds, data is pushed to the device.
* **Running**: Begin writing to the DAQ.

All input spans being written must share the same length and sample interval.
