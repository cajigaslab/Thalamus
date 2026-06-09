Troubleshooting & FAQ
=====================

FAQ
---

**Do I need hardware to try Thalamus?**
  No.  You can install the package, run the pipeline, generate signals with a WAVE
  node, record, and analyze -- all in software.  The :doc:`examples/index` are
  runnable with no acquisition hardware.

**What is a** ``.tha`` **file?**
  Thalamus's capture/recording format: an append-only log of length-prefixed
  protobuf records.  See :doc:`concepts` and convert it to HDF5/CSV/Parquet with the
  bundled :doc:`tools <tools>`.

**Which wheel do I download?**
  Pick the file on the `Releases page
  <https://github.com/cajigaslab/Thalamus/releases>`_ that matches your OS and Python
  -- e.g. ``...manylinux...`` for Linux, ``...win_amd64...`` for Windows,
  ``...macosx...arm64...`` for Apple Silicon.  The version in the docs is just an
  example; always grab the current release.

**Do Windows and Linux differ?**
  The tools and node types are the same; only the virtual-environment activation
  command and some third-party device drivers differ.

Install & startup
-----------------

**Verify your install.**  After installing the wheel, check the import works:

.. code-block::

   python -c "import thalamus; print('thalamus OK')"

Then launch the pipeline; you should see a window with an empty node list (see
:doc:`quickstart`):

.. code-block::

   python -m thalamus.pipeline

**grpc version conflicts.**  Thalamus bundles a specific ``grpc`` build.  Installing
into a fresh virtual environment (as the :doc:`quickstart` shows) avoids a mismatched
system ``grpcio`` that can cause import or connection errors.

**No window appears.**  The pipeline and task controller are Qt GUI applications and
need a display.  On a headless machine or over SSH without X forwarding no window can
open -- run on a desktop session, or forward the display.  If you only need data
conversion/analysis, the command-line :doc:`tools <tools>` and :doc:`examples
<examples/index>` do not require a display.

Running examples
----------------

The example scripts live in the `examples/
<https://github.com/cajigaslab/Thalamus/tree/main/examples>`_ folder of the
repository (they are not shipped inside the installed wheel).  Clone the repo, or
copy a script, then run it from the repo root, e.g. ``python
examples/synthetic_recording.py``.

Connecting tools to a pipeline
------------------------------

``registry`` and other clients connect to a **running** pipeline over gRPC (default
``localhost:50050``).  Start ``python -m thalamus.pipeline`` (or
``thalamus.task_controller``) first; if a client cannot connect, confirm the pipeline
is running and that the ``--address`` / ``--port`` match.
