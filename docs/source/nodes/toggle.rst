TOGGLE
======

The TOGGLE node is a transformer that behaves like a flip-flop driven by an analog
signal.  Each time the input makes a debounced rising crossing of the threshold, the
node flips its output state.  This turns a stream of pulses (such as repeated button
presses) into a latched on/off signal.

Properties
----------

* **Source**: The node supplying the input samples.
* **Threshold**: The level the input must rise through to trigger a toggle.  A
  debounce interval prevents a single noisy edge from toggling repeatedly.

The output is ``3.3`` when the node is in its "on" state and ``0`` when it is "off".
The state changes only on rising threshold crossings, so the same input level can
correspond to either output depending on the toggle history.
