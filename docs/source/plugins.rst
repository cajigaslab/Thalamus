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
  tree (see *Building and writing state* below).
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
* **Vulkan** -- access to the host's Vulkan objects for GPU compute/rendering inside
  a plugin (see *Vulkan access* below).

This is the basis for cross-node processing in compiled code: a transformer plugin
can subscribe to an upstream node, read its samples as they arrive, compute, and
inject results back into the pipeline.

.. note::

   The plugin API is a developer/integration surface.  For most data processing
   you can stay in Python with the :doc:`ALGEBRA <nodes/algebra>` / :doc:`LUA
   <nodes/lua>` nodes or by reading capture files (see :doc:`examples/index`); reach
   for a native plugin when you need new hardware support or performance-critical,
   low-latency computation inside the pipeline.

Entry points and lifecycle
--------------------------

A plugin is a shared library exporting one required symbol and one optional one
(both declared in ``plugin.h``):

.. code-block:: c

   /* Required.  Called once at load; returns a NULL-terminated array of
      node factories and receives the ThalamusAPI function table. */
   struct ThalamusNodeFactory** thalamus_get_node_factories(struct ThalamusAPI*);

   /* Optional.  Called when Thalamus shuts down and unloads the plugin --
      release threads, devices and any resources acquired at load time. */
   void thalamus_teardown(void);

Each ``ThalamusNodeFactory`` names a node type and provides ``create`` /
``destroy`` callbacks (plus optional ``prepare`` / ``cleanup`` hooks run around
pipeline start).  The node types a plugin registers appear in the node list next
to the built-in ones.

Beyond that library-level teardown, an individual node struct (``ThalamusNode``)
may set an optional ``predrop`` callback.  The host calls it when the node is about
to be replaced (its ``type`` config field changed) or during full pipeline
teardown, so the plugin can begin releasing resources (GPU handles, hardware,
in-flight I/O) asynchronously; once that work is complete, it calls
``node_predrop_ready(node)`` to signal the host it is safe to finish destroying the
node.  A node that leaves ``predrop`` unset (``nullptr``) is torn down immediately,
exactly as before this hook existed.

API versioning
--------------

``ThalamusAPI`` begins with an ``int32_t version`` field, and every function
pointer in the table is annotated in ``plugin.h`` with the version at which it was
added (the host currently passes ``131``).  The table is strictly append-only, so a
plugin built against an older header keeps working; a plugin that wants to use a
newer capability should guard it:

.. code-block:: c

   if (api->version >= 90) {
     struct ThalamusVkQueueLock* lock = api->lock_vulkan_queue();
     /* ... submit GPU work ... */
     api->unlock_vulkan_queue(lock);
   } else {
     /* fall back to CPU path */
   }

Building and writing state
---------------------------

Beyond reading and setting individual values, a plugin can build detached state
values and write them (or list entries) with a completion callback:

* ``state_make_dict()`` / ``state_make_list()`` -- create a new ``ThalamusState``
  dict/list that isn't attached to the graph yet.  Populate it with
  ``state_set_at_name_*`` / ``state_set_at_index_*`` calls, then attach it to the
  node's own state (e.g. with ``state_set_at_name_state`` or one of the
  ``state_push_*`` functions below) to make it visible in the UI and recordings.
* ``state_set_at_name_*_with_callback`` / ``state_set_at_index_*_with_callback`` --
  the same writes as ``state_set_at_name_*`` / ``state_set_at_index_*``, but taking
  a ``ThalamusPostCallback`` and a ``void*`` userdata pointer that is invoked once
  the write has actually taken effect.  For local state this happens
  synchronously, but state that is proxied to a remote/UI process can take a
  round-trip, so the callback is how a plugin sequences a dependent change without
  racing the update.
* ``state_push_*_with_callback`` -- append a value (state/string/int/float/null/
  bool) to a list state, with the same completion-callback semantics.  Use this to
  grow a list of settings (e.g. a per-channel configuration list) from a plugin.

.. code-block:: c

   /* Build {"gain": 2.0} and append it to a list state, then read the count back
      once the append has landed. */
   struct ThalamusState* entry = api->state_make_dict();
   struct ThalamusCharSpan gain_key = { "gain", 4 };
   api->state_set_at_name_float(entry, &gain_key, 2.0);
   api->state_push_state_with_callback(list_state, entry, on_pushed, user_data);

SDL windowing
-------------

A plugin can open and drive its own `SDL <https://www.libsdl.org/>`_ window rather
than relying on the host UI, via ``sdl_create_window`` / ``sdl_destroy_window``,
position/size/title accessors, ``sdl_vulkan_create_surface`` (for Vulkan
rendering into the window), clipboard access, system cursors, and event delivery:
``sdl_events_subscribe(callback, data)`` / ``sdl_events_unsubscribe`` delivers SDL3
events (mirrored as ``THALAMUS_SDL_Event`` in ``src/thalamus/plugin_window_event.h``)
to the plugin's callback.

.. _plugin-vulkan:

Vulkan access
-------------

Plugins can do GPU work using Thalamus's own Vulkan instance rather than creating
their own.  The API exposes:

* ``get_vulkan_instance()`` / ``get_vulkan_device()`` /
  ``get_vulkan_physical_device()`` -- the host's ``VkInstance``, ``VkDevice`` and
  ``VkPhysicalDevice``.
* ``get_vulkan_queue()`` -- the shared ``VkQueue``.
* ``create_vulkan_command_pool()`` -- a command pool for the shared queue's family.
* ``lock_vulkan_queue()`` / ``unlock_vulkan_queue()`` -- the queue is shared with
  Thalamus's own rendering (e.g. the image viewer), so **all submissions must be
  bracketed by the queue lock**:

.. code-block:: c

   struct ThalamusVkQueueLock* lock = api->lock_vulkan_queue();
   vkQueueSubmit(api->get_vulkan_queue(), 1, &submit_info, fence);
   api->unlock_vulkan_queue(lock);

This lets an image-processing plugin (for example, a GPU pupil detector or video
filter) run on the same device Thalamus renders with, without device-sharing
hazards.  A plugin can also open its own SDL window (see *SDL windowing* above) and
create a Vulkan surface for it with ``sdl_vulkan_create_surface`` to render into
directly, instead of using the shared image-viewer surface.
