CECI
====

The CECI node generates multi-channel biphasic electrical stimulation across one or
two output devices, with channel multiplexing and digital control.  Stimulation
parameters are declared through the stimulation interface; the node arms and
delivers the requested pulse trains on the configured devices.

Properties
----------

* **Device 0**: The name of the first output device used for stimulation.
* **Device 1**: The name of the second output device (for configurations that span
  two devices).

Stimulation declarations (channels, amplitudes, pulse timing) are sent to the node
through the stimulation API rather than as static configuration fields.  Use a
:doc:`STIM_PRINTER <stim_printer>` node to log the declarations that are issued.
