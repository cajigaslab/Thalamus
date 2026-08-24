Plugins (native extensions)
===========================

Beyond the Python pipeline, Thalamus supports **native plugins** -- shared libraries
(C/C++, or any language that can export a C ABI) that implement new node types and
load into the pipeline at runtime. Plugins participate in the same data graph as the
built-in nodes: they can produce data, consume it, and **read data directly from
other nodes** and **inject data back** through a stable C API.

.. admonition:: Breaking changes if you're updating an existing plugin

   Two changes since the previous docs revision affect the ABI of every existing
   plugin -- both must be addressed to keep an out-of-tree plugin building and
   loading:

   * **Entry point renamed.** A plugin's node-factory entry point must now be
     exported under the symbol name ``thalamus_get_node_factories`` (previously
     ``get_node_factories``).  The signature is unchanged:
     ``struct ThalamusNodeFactory** thalamus_get_node_factories(struct ThalamusAPI*)``.
     A plugin still exporting the old symbol name will fail to load.
   * **Strings are no longer raw C strings.** Every ``const char*`` /
     ``char*`` parameter and return value across ``ThalamusAPI`` and the node
     interfaces was replaced with ``struct ThalamusCharSpan`` (a
     ``{const char* data; uint64_t size; char owns_data;}`` span).  This touches
     most call sites a plugin makes -- for example ``state_get_string`` is now an
     out-parameter (``void state_get_string(struct ThalamusCharSpan*, struct
     ThalamusState*)``) rather than a return value, and ``ThalamusNodeFactory.type``
     is a ``ThalamusCharSpan`` instead of a ``const char*``.  Port any code that
     assumed a raw, nul-terminated ``char*``.

The C API
---------

The plugin C API is declared in ``src/thalamus/plugin.h``.  A plugin implements a
node factory and receives a ``ThalamusAPI`` table of function pointers that expose
the pipeline's capabilities.  The main capability groups are:

* **State** -- read and write the node's configuration
  (``state_get_*`` / ``state_set_*``) and subscribe to changes
  (``state_recursive_change_connect``).  Config values mirror what you see in the
  node UI.
* **Building state** -- construct new, detached state values and attach them to the
  tree (see *Writing state* below).
* **Other nodes** -- asynchronously obtain a handle to another node
  (``node_get_node`` with a selector), wait until it is ready
  (``node_ready_connect``), and track channel changes
  (``node_channels_changed_connect``).  ``node_get_node`` always invokes its
  callback asynchronously (posted to the io_context), even if the requested node
  already exists at call time -- it never calls back reentrantly from inside the
  call itself.
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
* **SDL windowing** -- open and drive your own SDL window (see *SDL windowing*
  below), for plugins that need a custom render surface or input handling instead
  of piggybacking on the host UI.
* **Vulkan** -- borrow the host's Vulkan instance/device/queue to render into your
  own SDL window (see *Vulkan* below).

This is the basis for cross-node processing in compiled code: a transformer plugin
can subscribe to an upstream node, read its samples as they arrive, compute, and
inject results back into the pipeline.

.. note::

   The plugin API is a developer/integration surface.  For most data processing
   you can stay in Python with the :doc:`ALGEBRA <nodes/algebra>` / :doc:`LUA
   <nodes/lua>` nodes or by reading capture files (see :doc:`examples/index`); reach
   for a native plugin when you need new hardware support or performance-critical,
   low-latency computation inside the pipeline.

Writing state
-------------

Beyond reading and setting existing state values, a plugin can now build and attach
new state values, and be notified once an assignment has actually propagated:

* ``state_make_dict()`` / ``state_make_list()`` -- construct a detached
  ``ThalamusState`` dict/list (not yet attached anywhere in the tree).  Attach it
  by assigning it into a parent with one of the setters below.
* ``state_set_at_name_*_with_callback`` / ``state_set_at_index_*_with_callback``
  (``state``, ``string``, ``int``, ``float``, ``null``, ``bool`` variants) -- the
  same semantics as the plain ``state_set_at_name_*`` / ``state_set_at_index_*``
  setters, but invoke a ``ThalamusPostCallback`` once the assignment has been
  applied and propagated to observers.
* ``state_push_*_with_callback`` (same value-type variants) -- append a value to a
  list (``ObservableList::push_back``), invoking the callback once the append has
  propagated.  This is how a plugin appends to a list-typed config value.

Use these together to build up a new sub-tree of config (e.g. a list of detected
items) and know when it is safe to rely on it being visible to other observers.

Versioning
----------

``ThalamusAPI`` begins with an ``int32_t version`` field.  The host sets it to an
ordinal identifying the newest function-pointer slot it populated (each function
pointer in ``plugin.h`` is annotated with its ordinal in a comment).  There is no
enforcement in either direction -- the host does not read anything back from the
plugin -- but a plugin author can guard use of newer capabilities, e.g.:

.. code-block:: c

   if (api->version >= 92) {
     // SDL functions are available on this host
   }

SDL windowing
-------------

A plugin can open and drive its own `SDL <https://www.libsdl.org/>`_ window rather
than relying on the host UI, via ``sdl_create_window`` / ``sdl_destroy_window``,
position/size/title accessors, ``sdl_vulkan_create_surface`` (for Vulkan
rendering into the window), clipboard access, system cursors, and event delivery:
``sdl_events_subscribe(callback, data)`` / ``sdl_events_unsubscribe`` delivers SDL3
events (mirrored as ``THALAMUS_SDL_Event`` in ``src/thalamus/plugin_window_event.h``)
to the plugin's callback.

Vulkan
------

A plugin that wants to render (typically into its own SDL window) can borrow the
host's existing Vulkan instance rather than creating its own:
``get_vulkan_instance()``, ``get_vulkan_device()``, ``get_vulkan_physical_device()``,
and ``get_vulkan_queue()``.  Because the host's render thread also submits work on
that same queue, guard any submission with ``lock_vulkan_queue()`` /
``unlock_vulkan_queue()`` around the critical section, and use
``create_vulkan_command_pool()`` to allocate command buffers.

Node lifecycle: predrop and teardown
-------------------------------------

* **``predrop``** -- a node struct (``ThalamusNode``) may set an optional
  ``predrop`` callback.  The host calls it when the node is about to be replaced
  (its ``type`` config field changed) or during full pipeline teardown, to give the
  plugin a chance to begin releasing resources (GPU handles, hardware, in-flight
  I/O) asynchronously.  Once that work is complete, the plugin calls
  ``node_predrop_ready(node)`` to signal the host it is safe to finish destroying
  the node.  A node that leaves ``predrop`` unset (``nullptr``) is torn down
  immediately, exactly as before this hook existed.
* **``thalamus_teardown``** -- an optional, library-level (not per-node) entry
  point.  If a plugin exports a symbol named ``thalamus_teardown``, the host calls
  it once, after all of that plugin's node factories have been cleaned up, as the
  pipeline shuts down -- useful for releasing any global/library state.
