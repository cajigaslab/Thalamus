Glossary
========

.. glossary::

   node
      The basic building block of a pipeline.  A small, configurable unit that
      produces, consumes, transforms, or controls data.  See the :doc:`Node
      Catalog <nodes/catalog>`.

   pipeline
      The running graph of nodes.  Started with ``python -m thalamus.pipeline``
      (or ``thalamus.task_controller``).

   node widget
      The custom configuration UI that appears for a selected node when its
      settings are richer than a few inline properties.

   subscription
      The link by which a consumer/transformer receives a producer's data.  You
      "subscribe" a consumer (e.g. STORAGE2) to the nodes whose data it should act
      on.

   capture file (``.tha``)
      Thalamus's recording format: a flat, append-only sequence of length-prefixed
      ``StorageRecord`` protobuf messages.  See :doc:`concepts`.

   span
      Within an analog record, a named slice of the flat ``data`` array that maps a
      range of samples to one channel.

   hydrate
      Convert a ``.tha`` capture into a single HDF5 file for analysis, with
      ``python -m thalamus.hydrate``.

   steady clock
      The monotonic nanosecond time base Thalamus stamps records with.  It measures
      intervals precisely but is **not** a wall-clock date (use a WALLCLOCK node to
      anchor to absolute time).

   task
      One behavioral trial paradigm run by the :doc:`Task Controller
      <task_controller>` (e.g. a delayed reach).

   task cluster
      A weighted group of tasks the Task Controller samples from to schedule trials.

   subject view / operator view
      The two windows of behavioral tools: the **subject view** shows what the
      subject sees; the **operator view** adds operator-only overlays and controls.

   pin
      In :doc:`Eye Calibration <eye_calibration>` Angular-Scaling mode, a control
      point that sets the gaze scale/rotation at a particular direction.

   plugin
      A native (C/C++/Rust) shared library that adds a node type to the pipeline via
      the :doc:`plugin API <plugins>`.
