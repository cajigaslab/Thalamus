Plugins (native extensions)
===========================

Beyond the Python pipeline, Thalamus supports **native plugins** -- shared libraries
(C/C++/Rust) that implement new node types and load into the pipeline at runtime.
Plugins participate in the same data graph as the built-in nodes: they can produce
data, consume it, and now **read data directly from other nodes** and **inject data
back** through a stable C API.

The C API
---------

The plugin C API is declared in ``src/thalamus/plugin.h``.  A plugin implements a
node factory and receives a ``ThalamusAPI`` table of function pointers that expose
the pipeline's capabilities.  The main capability groups are:

* **State** -- read and write the node's configuration
  (``state_get_*`` / ``state_set_*``) and subscribe to changes
  (``state_recursive_change_connect``).  Config values mirror what you see in the
  node UI.
* **Other nodes** -- asynchronously obtain a handle to another node
  (``node_get_node`` with a selector), wait until it is ready
  (``node_ready_connect``), and track channel changes
  (``node_channels_changed_connect``).
* **Reading analog data** -- given an analog node handle, read its channels in the
  type the source provides:

  * ``data(channel)`` -- ``double`` samples
  * ``short_data(channel)`` -- 16-bit integer samples
  * ``int_data(channel)`` -- 32-bit integer samples
  * ``ulong_data(channel)`` -- 64-bit unsigned integer samples (see
    :ref:`ulong_data <ulong-data>`)

  along with ``num_channels()``, ``name(channel)`` and ``sample_interval_ns(channel)``.
* **Injecting / passing through** -- inject analog data into the graph through a
  callback-based interface, and pass requests through to other nodes (for example,
  forwarding device queries to an OCULOMATIC camera) without blocking shutdown.
* **Timing & I/O** -- a steady ``time_ns`` clock, timers, an async I/O context, and
  serial-port helpers for hardware plugins.

This is the basis for cross-node processing in compiled code: a transformer plugin
can subscribe to an upstream node, read its samples as they arrive, compute, and
inject results back into the pipeline.

.. note::

   The plugin API is a developer/integration surface.  For most data processing
   you can stay in Python with the :doc:`ALGEBRA <nodes/algebra>` / :doc:`LUA
   <nodes/lua>` nodes or by reading capture files (see :doc:`examples/index`); reach
   for a native plugin when you need new hardware support or performance-critical,
   low-latency computation inside the pipeline.
