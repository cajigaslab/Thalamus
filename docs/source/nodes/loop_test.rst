LOOP_TEST
=========

The LOOP_TEST node is a diagnostic signal generator.  It emits a sinusoidal test
signal (a single ``Sin`` output channel) whose frequency is taken from an input
value, which is useful for exercising and validating signal routing through a
pipeline.

Properties
----------

* **Source**: The node whose value sets the frequency of the generated sinusoid.
* **Channel**: The channel of the source to read the frequency from.

The most recent value read from the selected source/channel is used as the
frequency of the emitted ``Sin`` signal.
