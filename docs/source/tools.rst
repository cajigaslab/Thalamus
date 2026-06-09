Command-line tools
==================

Thalamus ships several command-line modules.  Each is run with ``python -m``.

registry — live state editing
------------------------------

The ``registry`` tool reads and edits the configuration of a **running** Thalamus
pipeline over gRPC, using `JSONPath <https://github.com/h2non/jsonpath-ng>`_ to
address any part of the state tree.  This lets you inspect or change node parameters
(thresholds, addresses, task settings, ...) live, without restarting.  Reach for it
to tune a parameter mid-experiment, or to script changes to a running pipeline.

.. code-block::

   python -m thalamus.registry -a ADDRESS -p JSONPATH [-s VALUE] [-d]

* ``-a, --address`` -- the pipeline address (default ``localhost:50050``).
* ``-p, --path`` -- a JSONPath expression selecting the element(s) to act on.
* ``-s, --set`` -- a JSON value to assign to the selected element(s).
* ``-d, --delete`` -- delete the selected element(s).

With neither ``--set`` nor ``--delete``, the tool prints the current value.

Examples::

   # Print the whole node list
   python -m thalamus.registry -p '$.nodes'

   # Read one node's Running flag (assuming a node named "wave")
   python -m thalamus.registry -p "$.nodes[?(@.name=='wave')].Running"

   # Start that node
   python -m thalamus.registry -p "$.nodes[?(@.name=='wave')].Running" -s true

Because the pipeline state is observable, assignments propagate to every connected
client immediately.

Data tools
----------

These convert and inspect ``.tha`` capture files (see :doc:`concepts` and
:doc:`examples/index`):

* ``python -m thalamus.record_reader2 FILE`` -- print the records in a capture file.
* ``python -m thalamus.dataframe -n NODE -i FILE`` -- export a node's analog or text
  channels to CSV, Parquet, and other tabular formats.
* ``python -m thalamus.hydrate FILE`` -- convert an entire capture into a single
  HDF5 file (per-channel ``data`` plus ``received`` timing).

Applications
------------

* ``python -m thalamus.pipeline`` -- the data pipeline (no task controller).
* ``python -m thalamus.task_controller`` -- the pipeline plus the behavioral
  :doc:`Task Controller <task_controller>`.
* ``python -m thalamus.eye_calibration`` -- the interactive
  :doc:`Eye Calibration <eye_calibration>` tool.
