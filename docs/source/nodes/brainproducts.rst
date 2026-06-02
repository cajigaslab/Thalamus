BRAINPRODUCTS
=============

The BRAINPRODUCTS node streams EEG/biosignal data from a Brain Products amplifier
through the BrainVision Amplifier SDK and republishes it as an analog stream.  It is
a generator.

The amplifier is detected and its channels, sample rate, and resolution are read
from the device through the SDK, so there are no manual channel-configuration
fields.

Properties
----------

* **Running**: Connect to the amplifier and begin acquisition.

.. note::

   This node depends on the proprietary BrainVision Amplifier SDK library being
   present; it is only available where that SDK can be loaded.
