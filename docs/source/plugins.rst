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

.. admonition:: Source-level change in 1.0.47

   ``plugin.h`` no longer requires C++ and can be included from plain C.  To make
   that possible the enumerators of ``ThalamusStateType``, ``ThalamusStateAction``
   and ``ThalamusImageFormat`` were prefixed with their enum name -- for example
   ``Dict`` is now ``ThalamusStateType_Dict``, ``Set`` is
   ``ThalamusStateAction_Set`` and ``Gray`` is ``ThalamusImageFormat_Gray``.  The
   numeric values are unchanged, so already-built plugins keep working; only source
   that names the old enumerators needs updating.

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
* **Multithreaded signaling** -- an *off-main signaler* for nodes that announce new
  data from their own threads (see *Signaling from your own thread* below).
* **Dialogs** -- show an info/warning/error/fatal message box in the UI with
  ``dialog_show`` (see *Dialogs and removing state* below).
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

A plugin may also export an optional ``thalamus_get_node_factory_version`` function
(``int32_t thalamus_get_node_factory_version(void)``).  A plugin that does not
export it is treated as version 0 and its factories are constructed with
``ThalamusNodeFactory.create``.  A plugin that returns ``1`` or higher has its
factories constructed with ``ThalamusNodeFactory.create2`` instead, which takes an
extra ``void*`` argument identifying the host-side node being created.

Two more optional exports declare how much of the node structs a plugin was built
with.  Each returns the number of pointers after a given field, and a missing
export means ``0``; the host never reads past what the plugin declares:

* ``int32_t thalamus_get_node_version(void)`` -- pointers after
  ``ThalamusNode.signals_offmain`` (version ``1`` adds ``blob``).
* ``int32_t thalamus_get_analog_node_version(void)`` -- pointers after
  ``ThalamusAnalogNode.name`` (adds ``buffer``, ``encoding``, ``format``,
  ``encoded_count`` and ``channels_changed``).  A plugin that consumes analog data
  from host nodes can check ``api->analog_node_version()`` (API version 140) to see
  how many of those the host provides.

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
added (the host currently passes ``153``).  The table is strictly append-only, so a
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

Signaling from your own thread
------------------------------

A node that produces data on its own thread sets ``signals_offmain`` to ``1`` in its
``ThalamusNode`` struct.  To announce new data from that thread, create an *off-main
signaler* for the node and call ``ready`` on it instead of ``node_ready_offmain``:

* ``node_offmain_signaler_create(node)`` / ``node_offmain_signaler_destroy(signaler)``
  -- create and free a signaler.  A new signaler starts **blocked**.
* ``node_offmain_signaler_unblock(signaler)`` / ``node_offmain_signaler_block(signaler)``
  -- allow or suppress signaling.  Block the signaler before destroying or tearing
  down the node so a late ``ready`` call cannot reach a node that is going away.
* ``node_offmain_signaler_ready(signaler)`` -- notify downstream consumers that new
  data is available.  Returns ``0`` if the signaler is blocked (before or while the
  call waits) and ``1`` otherwise.  Consumers that run on the main thread are
  notified on the main thread, and the call waits until that has happened (or the
  signaler is blocked), so the producer thread never races ahead of the consumers.
  Use the return value to stop your thread's loop when the node is shutting down.

This API requires ``api->version >= 136``.

Dialogs and removing state
--------------------------

* ``dialog_show(title, message, type)`` (version 137) -- show a message box in the
  UI.  ``type`` is one of ``ThalamusDialogType_Info``, ``_Warn``, ``_Error`` or
  ``_Fatal``; ``title`` and ``message`` are ``ThalamusCharSpan`` pointers.
* ``state_remove_at_name(state, key, callback, data)`` and
  ``state_remove_at_index(state, index, callback, data)`` (version 138/139) --
  delete a dictionary entry or list element, with the same completion-callback
  semantics as the ``*_with_callback`` writes above.

.. code-block:: c

   struct ThalamusCharSpan title = { "Calibration", 11 };
   struct ThalamusCharSpan message = { "Camera disconnected", 19 };
   api->dialog_show(&title, &message, ThalamusDialogType_Warn);

   struct ThalamusCharSpan key = { "gain", 4 };
   api->state_remove_at_name(entry, &key, on_removed, user_data);

Analog formats, encoded audio and blobs
---------------------------------------

An analog message can mix channels of different types.  ``ThalamusAnalogNode.format(node, channel)``
returns a ``ThalamusAnalogFormat`` (``Double``, ``Short``, ``Int``, ``ULong`` or
``Encoded``) saying which of the ``*_data`` accessors holds that channel's samples;
``Encoded`` channels have no samples there and live in ``buffer(node)``, whose
``encoding(node)`` is ``ThalamusAnalogEncoding_AAC``, ``_Opus`` or ``_FLAC`` (FLAC frames
with no STREAMINFO; each frame header gives the sample rate and bits per sample) (``encoded_count``
samples per channel).  ``channels_changed(node)`` is true on the first message whose
channels (count, names, formats or sample intervals) differ from the previous one;
it replaces the ``node_channels_changed*`` API functions, which are now deprecated
no-ops.

A node with the ``THALAMUS_MODALITY_BLOB`` modality (set ``ThalamusNode.blob``,
node version 1) emits arbitrary binary payloads: ``body``, ``mime``, ``stream`` and
``has_blob_data`` (check it before reading; not every message carries a blob).
:doc:`STORAGE2 <nodes/storage2>` records blobs.

.. code-block:: c

   if (api->analog_node_version() >= 5 && node->analog->channels_changed(node)) {
     /* re-read channel names and formats */
   }

Other API additions: ``state_root()`` (version 142) returns a new reference to the
root of the whole state tree, ``trace_event_begin_static(name)`` (version 141) is a
cheaper ``trace_event_begin`` for names that stay valid while the plugin is loaded,
and ``analog_node_version()`` (version 140).

Remote times
------------

Messages that came from another Thalamus through a :doc:`REMOTE <nodes/remote>` or
:doc:`REMOTE_LOG <nodes/remote_log>` node carry the time they had on the Thalamus that
first produced them.  ``remote_time_ns(node)`` returns it, or ``0`` if the message was
produced locally.  It is available on ``ThalamusAnalogNode`` (analog node version 6)
and on ``ThalamusImageNode``, ``ThalamusTextNode`` and ``ThalamusMocapNode`` (version 1
of each).  Those three versions count the pointers after ``has_image_data``,
``has_text_data`` and ``has_motion_data``; a plugin exports
``thalamus_get_image_node_version``, ``thalamus_get_text_node_version`` and
``thalamus_get_mocap_node_version`` for its own nodes (missing exports mean ``0``), and
checks the host's with ``api->image_node_version()`` (147), ``text_node_version()``
(148) and ``mocap_node_version()`` (149).  As with analog data, check ``has_image_data``,
``has_text_data`` or ``has_motion_data`` before reading a message.

Events, logs and build information
----------------------------------

``events_connect(callback, data)`` (144) calls ``callback`` with a ``ThalamusEvent``
(``payload`` and ``time_ns``) for every event sent to Thalamus, and
``log_connect(callback, data)`` (145) with a ``ThalamusLog`` (``text``, ``time_ns`` and
``remote_time_ns``) for every log message, whether it came from the ``log`` gRPC call
or a node.  Callbacks may run on any thread and the spans are valid only during the
call.  Both return a connection that ``message_disconnect(connection)`` (146) ends;
once it returns the callback isn't running and won't be called again, so its ``data``
can be freed.  Don't call it from the callback.

``log(text)`` (153) sends text to Thalamus's log like the ``log`` gRPC call: it reaches
``log_connect`` listeners, the log view and recordings, timed now with no remote time.
It can be called from any thread and copies the text.

``build_type(span)`` (150), ``thalamus_version(span)`` (151) and ``commit(span)`` (152)
set a ``ThalamusCharSpan`` to the same build type, version and git commit that
:doc:`STORAGE2 <nodes/storage2>` records; the text is static (``owns_data`` is ``0``).

.. code-block:: c

   static void on_log(const struct ThalamusLog* log, void* data) {
     /* log->text is only valid during this call */
   }

   struct ThalamusMessageConnection* conn = api->log_connect(on_log, NULL);
   struct ThalamusCharSpan text = { "plugin started", 14 };
   api->log(&text);
   /* ... later ... */
   api->message_disconnect(conn);

Node requests
-------------

``node_request(node, request, callback, data)`` (version 143) sends a JSON request to
another node's ``process``, like the ``node_request`` gRPC call.  ``node`` is one the
plugin received from ``node_get_node`` (take a reference with ``node_inc_ref`` to use
it after that callback returns), and the call must be made on the main thread.  The
``request`` isn't retained.  ``callback`` is called exactly once, on the main thread
and never before ``node_request`` returns, with a ``ThalamusNodeRequestStatus``:

* ``ThalamusNodeRequestStatus_Ok`` -- the node responded; ``response`` holds the JSON
  (valid only during the call, ``json_inc_ref`` it to keep it).
* ``ThalamusNodeRequestStatus_Destroyed`` -- the node was destroyed before the request
  was sent.
* ``ThalamusNodeRequestStatus_Dropped`` -- the node discarded the request without
  responding, e.g. because it was destroyed first.

``response`` is null unless the status is ``Ok``.

.. code-block:: c

   static void on_response(enum ThalamusNodeRequestStatus status,
                           const struct ThalamusJson* response, void* data) {
     if (status != ThalamusNodeRequestStatus_Ok) {
       return; /* node went away */
     }
     /* read response */
   }

   api->node_request(node, request, on_response, NULL);

Image formats
-------------

``ThalamusImageNode`` formats are given by ``ThalamusImageFormat``:
``Gray``, ``RGB``, ``YUYV422``, ``YUV420P``, ``YUVJ420P``, ``NV12``, ``BGR``,
``MJPEG``, ``MPEG1``, ``MPEG4``, ``Gray16``, ``RGB16``, ``H264`` and ``VP9`` (each
prefixed with ``ThalamusImageFormat_``).  ``MJPEG``, ``MPEG1``, ``MPEG4``, ``H264``
(Annex B, one frame per message) and ``VP9`` carry already-compressed frames;
:doc:`STORAGE2 <nodes/storage2>` writes ``MPEG1``, ``MPEG4``, ``H264`` and ``VP9``
streams as-is instead of re-encoding them.  ``H264`` and ``VP9`` (and the ``Opus``
analog encoding) are enum values only: Thalamus has no codec for them, so
``hydrate``, the record reader and the image viewer cannot decode them.  A plugin
such as thalamus-contrib's ``MEDIA_CONVERTER`` does the encoding and decoding.

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
