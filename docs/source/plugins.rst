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
* **Vulkan** -- access to the host's Vulkan objects for GPU compute/rendering inside
  a plugin (see :ref:`plugin-vulkan`).

This is the basis for cross-node processing in compiled code: a transformer plugin
can subscribe to an upstream node, read its samples as they arrive, compute, and
inject results back into the pipeline.

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

.. note::

   The load symbol was previously named ``get_node_factories``; it is now
   ``thalamus_get_node_factories``, and plugins exporting only the old name will
   fail to load.  ``thalamus_teardown`` is looked up at shutdown and simply
   skipped if absent, so existing plugins keep working without it.

API versioning
--------------

``ThalamusAPI`` begins with an ``int32_t version`` field, and every function
pointer in the table is annotated in ``plugin.h`` with the version at which it was
added (the host currently passes ``91``).  The table is strictly append-only, so a
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
hazards.

.. note::

   The plugin API is a developer/integration surface.  For most data processing
   you can stay in Python with the :doc:`ALGEBRA <nodes/algebra>` / :doc:`LUA
   <nodes/lua>` nodes or by reading capture files (see :doc:`examples/index`); reach
   for a native plugin when you need new hardware support or performance-critical,
   low-latency computation inside the pipeline.
