# AGENTS.md

Notes for coding agents working on Thalamus. See CONTRIBUTING.md for the
human-oriented setup guide.

## Layout

- `src/thalamus.cpp`, `src/main.cpp`: the native process (`native thalamus`):
  gRPC server, node graph, HTTP/WebSocket server.
- `src/thalamus/*_node.{hpp,cpp}`: C++ nodes. `node_graph_impl.cpp` holds the
  node registry, the plugin (extension) loader and the C API implementation.
- `src/thalamus/plugin.h`: the C ABI used by extension libraries (e.g.
  thalamus-contrib, a Rust plugin). `modalities.h` holds the modality bits.
- `src/hydrate.cpp`: offline exporter for recordings (CSV/HDF5/video).
- `proto/thalamus.proto`: the gRPC/storage schema, shared by C++, Python and
  .NET.
- `thalamus/`: the Python package. `thalamus.pipeline` and
  `thalamus.task_controller` are the two UI entry points; both launch
  `native.exe` (and `dotnet.exe` if present) as child processes.
- `dotnet/`: the .NET process (Delsys support), built by CMake.

## Building

- Full build and wheel: `python prepare.py` once, then
  `python -m build -n -w -Crelease` (see CONTRIBUTING.md).
- Incremental C++ builds: `ninja -C build/clang-debug native` (or
  `build/clang-release`). `hydrate` is a separate target:
  `ninja -C build/clang-debug native hydrate`. The `native` target also builds
  the .NET project, and copies `native.exe` into `thalamus/`, which is what the
  Python apps launch.
- On Windows, run builds from PowerShell, not Git Bash: Git Bash puts its own
  `link.exe` ahead of MSVC's.
- Building `dotnet/dotnet.sln` directly can fail with "solution configuration
  Debug|x64 is invalid" if `Platform=x64` is in the environment; build
  `dotnet/dotnet/dotnet.csproj` with `-p:Platform=AnyCPU` instead.
- Python protobuf bindings (`thalamus/thalamus_pb2.py`) are regenerated
  automatically by the entry points when `proto/thalamus.proto` is newer;
  `python thalamus/build.py` regenerates them by hand. They aren't tracked.
- Changing `proto/thalamus.proto` rebuilds nearly every C++ file. With the
  default parallelism clang can run the machine out of memory
  (`LLVM ERROR: out of memory`, not a compile error); use
  `cmake --build build/clang-debug --target native -j 4`.
- Non-CI Windows builds copy `native.pdb` next to `native.exe` in `thalamus/`
  (skipped when `THALAMUS_CI` is set). Crash dumps can only be symbolized with
  the PDB from the same build.

## C++ conventions

- Clang builds use `-Weverything` minus a short list (see `WARNING_FLAGS` in
  CMakeLists.txt). Don't add warnings. Common ones:
  - `-Wswitch` / `-Wswitch-enum`: switches over enums must name every value.
    Protobuf enums are open and have `*_SENTINEL_DO_NOT_USE_` values, so
    convert protobuf enums with `if` chains that return a fallback (or
    `std::optional`) rather than switches.
  - Non-void functions that switch over an enum need a statement after the
    switch (e.g. `THALAMUS_ABORT("Unknown ...")`) for values outside the
    enum.
  - `-Wglobal-constructors` / `-Wexit-time-destructors`: use a function-local
    `static auto *x = new T();` instead of a global object.
  - `-Wcast-function-type-strict`: cast function pointers through `void*`.
  - `-Wnrvo`, `-Wnewline-eof`, `-Woverloaded-virtual`.
- Wrap third-party includes in
  `#pragma clang diagnostic push` / `ignored "-Weverything"` / `pop`, as the
  existing headers do.
- Source files use CRLF line endings; keep them when editing.
- Many nodes inherit several modality interfaces at once (e.g. `ImageNode` and
  `AnalogNode`). A member name used by two interfaces (types or virtual
  functions) is ambiguous or hides overloads in those nodes, so give new
  interface members distinct names (hence `AnalogNode::AnalogFormat` /
  `analog_format()` next to `ImageNode::Format` / `format()`).

## Plugin C API (`plugin.h`)

Plugins and Thalamus are built separately, so every struct in `plugin.h` is an
ABI. Rules:

- Only append fields; never reorder, remove or retype them.
- The header must stay valid C (thalamus-contrib runs bindgen on it with
  `-x c`): write `enum X` / `struct X` in declarations.
- Every appended field is versioned, and the reader checks the version before
  touching it (reading past the end of an older, smaller struct is undefined
  behavior):
  - `ThalamusAPI::version` is the number of API function pointers the host
    provides; new functions are appended with a `// N` index comment and
    `thalamus_api.version` is bumped in `node_graph_impl.cpp`.
  - Plugins export `thalamus_get_node_factory_version`,
    `thalamus_get_node_version`, `thalamus_get_analog_node_version`,
    `thalamus_get_image_node_version`, `thalamus_get_text_node_version` and
    `thalamus_get_mocap_node_version`: the number of pointers after
    `ThalamusNodeFactory::plugin_impl`, `ThalamusNode::signals_offmain`,
    `ThalamusAnalogNode::name`, `ThalamusImageNode::has_image_data`,
    `ThalamusTextNode::has_text_data` and `ThalamusMocapNode::has_motion_data`
    respectively. Missing exports mean 0.
  - For structs Thalamus provides to plugins (e.g. `ThalamusAnalogNode` for
    C++ nodes), the host reports its version through an API function
    (`ThalamusAPI::analog_node_version`, `image_node_version` (slot 147),
    `text_node_version` (slot 148), `mocap_node_version` (slot 149)), so
    plugins can check it.
  - `build_type`/`thalamus_version`/`commit` (slots 150-152) set a
    `ThalamusCharSpan` to the static
    `THALAMUS_BUILD_TYPE`/`THALAMUS_VERSION`/`THALAMUS_GIT_COMMIT_HASH`
    STORAGE2 records, so plugins can record them too.
- `remote_time_ns` (analog node version 6, image, text and mocap node
  version 1) is the message's time on the Thalamus that first produced it,
  if it came from another one, else 0
  (`AnalogNode`/`ImageNode`/`TextNode`/`MotionCaptureNode::remote_time`).
  `ExtNode` has one `remote_time()` for all four interfaces, so it reads the
  one whose `has_*_data` is set.
- `events_connect`/`log_connect`/`message_disconnect` (slots 144-146) call a
  plugin with a `ThalamusEvent` (payload, time) or `ThalamusLog` (text,
  time, remote time) for every event or log message, on any thread. `MessageListener` calls under a mutex that
  `message_disconnect` takes, so the plugin can free its data once that
  returns.
- `state_root()` (slot 142) returns a new reference to the root of the state
  tree (the whole config), which `ThalamusAPIImpl` holds while the node graph
  exists. Plugins on an older Thalamus walk `state_parent` from any state
  until it returns null.
- `node_request(node, request, callback, data)` (slot 143) sends a JSON
  request to a node's `process`, like the `node_request` gRPC call. The node
  is one the plugin got from `node_get_node` (held with `node_inc_ref` to use
  it later). Call it on the main thread. Node wrappers hold a `weak_ptr` to
  their node, so a request to a destroyed node reports `Destroyed`, and
  `get_node_ref` doesn't reuse an expired wrapper for a new node at the same
  address. A `NodeRequestCall` owned by the closure passed to `process`
  guarantees the callback runs exactly once: with the response (`Ok`),
  `Destroyed`, or `Dropped` if the node destroys the closure without
  responding. The callback is always posted to the io_context, so it runs on
  the main thread and never before `node_request` returns.
- Spans returned through the API are borrowed and only valid during the
  `ready`/`get_node` callback.
- A node with a modality doesn't have that data in every message (an image
  node can emit audio-only messages and vice versa). Check
  `has_image_data`/`has_analog_data` before reading; e.g. `channel_info`
  calls `num_channels()` without checking, which is only safe because
  plugins keep analog data on every message.
- thalamus-contrib vendors `plugin.h` and `modalities.h` in
  `rust/include/thalamus/`; copy them over after changing them here.

## Remote times

- `AnalogResponse`, `Image`, `XsensResponse` and `Text` carry the serving
  node's `time` and `remote_time`. A REMOTE node's `remote_time()` is the message's
  `remote_time` if set (the message already came through a REMOTE node
  upstream), else its `time`, so it stays the time on the Thalamus that
  first produced the data across chains of REMOTE nodes. RemoteNode has one
  `remote_time()` for its analog, image and mocap interfaces, so it's set on
  the main thread right before each broadcast (0 for its own Ping data).
  STORAGE2 records it for analog, image, mocap and text.

## State

- `State::connect`-style callbacks (`state_recursive_change_connect`) fire for
  changes anywhere below the connected collection, not just its own keys.
  Filter on the source collection when only the node's own keys matter.
- `ThalamusState*` wrappers are interned per collection (`get_state_ref`), so
  two wrappers of the same live collection compare equal by pointer.
- Setting state inside a state callback re-enters callbacks synchronously.
- `thalamus.pipeline` sets every node's `Running` to false when it loads a
  config; nodes are started explicitly (UI or `thalamus.registry`).
- Shutdown destroys the node graph after `io_context.run()` returns, not in a
  handler: on Windows `io_context.stop()` still runs handlers queued before
  it, so anything posted while nodes are destroyed (e.g. a plugin thread's
  last `post_to_main`) would run after `~Impl` deleted the `ThalamusState`
  maps. `state_dec_ref` and friends must only run on the main thread while
  the graph exists.

## Node lookup

- A `NodeSelector` matches on `name`, `type`, or both (both must match when
  both are set). One with neither is invalid (`valid_node_selector`).
- `NodeGraph::get_node(selector, callback)` and `get_node_scoped` call back
  once a matching node exists: posted to the io_context if it exists now,
  otherwise when one is created or renamed/retyped to match. They never call
  back for an invalid selector, so an unset `Source` just waits.
- `get_node_scoped`'s callback only fires while the returned
  `NodeConnection` is alive; keep it.
- The callback can still receive an expired `weak_ptr` if the node is
  removed before the posted delivery runs, so check `lock()`. The plugin C
  API (`node_get_node`) wraps whatever it receives, so a null there reaches
  the plugin as a wrapper around a null node.

## Image formats

- An encoded image message (MPEG4, H264, VP9) holds exactly one coded
  picture: one packet as FFmpeg's encoders and decoders define it (a VP9
  superframe, a hidden frame plus a shown one, counts as one). Messages are in
  decode order, and each one's time is when its picture was acquired (see
  Analog data), so with B-frames times aren't increasing. Encoders emit no
  message until they have a packet, and one message per packet when they
  output several at once; thalamus-contrib's image converter times each
  packet from the frame it encodes, and decodes into presentation order.
- `H264` (protobuf `Image.Format`, `ThalamusImageFormat_H264` in `plugin.h`,
  `ImageNode::Format::H264`) is Annex B H.264, one frame per message. It's an
  enum only: Thalamus passes it through nodes, gRPC and remote nodes, but has
  no H.264 codec, so hydrate, the record reader, STORAGE's encoder and the
  image viewer treat it as unsupported (STORAGE2 records it as is).
  thalamus-contrib's
  MEDIA_CONVERTER encodes and decodes it.
- thalamus-contrib encodes H.264 with Cisco's OpenH264 binary, which isn't
  shipped: Cisco's H.264 patent license only covers it when it's downloaded
  separately and users can turn it off. `thalamus/openh264.py` downloads it
  into `~/.thalamus` at pipeline and task controller startup (HTTP only,
  retried, checked against pinned SHA-256s). Preferences > H264
  (`pipeline/openh264_dialog.py`) is the required control, with Cisco's
  attribution next to it and the full `BINARY_LICENSE.txt` text. Opting out
  only creates `~/.thalamus/.no_openh264`; while it exists, startup deletes
  the binary (before native.exe loads it) instead of downloading it, so a
  running Thalamus keeps encoding until it's restarted. Opting in deletes the
  marker and downloads. thalamus-contrib uses the binary whenever it's
  present.
- STORAGE2 records H264 and VP9, like MPEG4, as is (identity encoder).
- `VP9` (`Image.Format`, `ThalamusImageFormat_VP9`, `ImageNode::Format::VP9`)
  and the `Opus` analog encoding (`AnalogResponse.Encoding`,
  `ThalamusAnalogEncoding_Opus`, `AnalogNode::Encoding::Opus`) are likewise
  enums only; thalamus-contrib's MEDIA_CONVERTER encodes and decodes them.
  Opus buffers are packets framed with MPEG-TS Opus control headers.

## Analog data

- `AnalogNode::analog_format(channel)` says which `*data` function holds a
  channel's samples (`Double`, `Short`, `Int`, `ULong`), or `Encoded` for
  channels whose samples are in `buffer()` (encoding given by `encoding()`,
  `encoded_count()` samples per channel). The default derives every channel's
  format from the `is_*` functions. Use `visit_channel` to read a channel in
  its own type; `visit_node` assumes one type for every channel and reads
  nothing from channels of another type in mixed-format messages (e.g.
  thalamus-contrib's MEDIA_CONVERTER: f64 stats channels plus i16 audio). The
  `graph` and spectrogram RPCs use `visit_channel`; storage, lua and
  samplemonitor still use `visit_node`.
- A message's time is when its data was acquired (for analog data, its last
  sample), not when a node encoded, decoded or otherwise processed it. An
  encoded analog message's packets hold audio from before the input that
  completed them, so its time is that of the last sample in its packets and
  `encoded_count()` the number of samples they hold; encoders emit no message
  until they have packets. Readers can place encoded audio from those alone.
  thalamus-contrib's converters keep the time of their input for latency
  stats internally, never on the wire.
- `AnalogNode::channels_changed()` is true on the first message whose
  channels (count, names, formats or sample intervals) differ from the
  previous one's; nodes pass it through `inject_analog(..., channels_changed)`.
  Subscribers check it on every message and treat the first message they see
  as changed.
- On the wire, `Span.format` gives each channel's format. `Double` is 0, so
  records written before the field existed still read correctly: use
  `span_format()` in `analog_proto.hpp`, which falls back to
  `is_int_data`/`is_ulong_data`. Writers only set those flags when every
  array-backed channel shares that array.
- The gRPC `analog` stream sends all channels as scaled doubles in `data`
  unless the request sets `native_formats`; the Python UI relies on the
  default. Encoded channels are always marked `Encoded` with an empty span.
- `analog_proto.hpp` has the shared protobuf conversions and
  `append_native_channel` / `finish_native` for writing channels natively.
- Storage2 writes channels natively in uncompressed mode. Its compressed
  analog mode is being phased out and doesn't record encoded buffers.

## gRPC and TLS

- `--cert`, `--key`, `--ca` and `--server-name` (default `thalamus.internal`)
  configure TLS for native thalamus, `thalamus.pipeline`,
  `thalamus.task_controller`, `thalamus.eye_calibration`, the Python image
  viewer and `dotnet.exe`. Without them everything runs without TLS.
- Implementations: `src/thalamus/grpc_tls.{hpp,cpp}` (C++),
  `thalamus/grpc_tls.py` (Python), `dotnet/dotnet/Util.cs` (.NET). Create
  channels through them (`create_grpc_channel`, `grpc_tls.aio_channel`,
  `NodeGraph::get_channel`), never with insecure credentials directly.
- Clients check every server's certificate against `--server-name`, not the
  address they dial, so certificates need
  `subjectAltName=DNS:thalamus.internal` (or whatever name is configured),
  not localhost or IPs.
- Native thalamus binds its gRPC server to `--ip`; the Python launchers pass
  `--ip 0.0.0.0` (and `--open` to dotnet) when `--open` or TLS is enabled. The
  HTTP/WebSocket server always binds to 127.0.0.1.
- Child processes get the TLS flags from `grpc_tls.command_line_args()`.

## gRPC handlers

- Node graph and node objects live on the io_context (main) thread; handlers
  `post` work there. A lambda posted (or passed to `get_node`) from a handler
  must not capture the handler's stack by reference (`[&]`): when the client
  cancels, a sync handler returns and its thread may exit before the callback
  runs, which writes into freed stack memory. `get_modalities` and
  `get_recommended_channels` still do this. Put shared state behind a
  `shared_ptr` instead.
- Callback-API reactors (`ServerUnaryReactor`, `ServerBidiReactor2`, the
  `NodeSession` family in `node_session.hpp`):
  - Call `Finish` exactly once on every path, including `OnCancel`; guard it
    with a flag checked and set under the state mutex, and call `Finish` after
    releasing the mutex. After `Finish`, don't touch the request, the
    response or the reactor (`OnDone` deletes it).
  - Don't hold a lock while calling `node->process`: the default
    `Node::process` answers synchronously, and the reply callback takes the
    same lock.
  - Once a lock is released the session may be destroyed; copy `state` and
    anything else needed into locals first, and recheck `joining` after
    relocking.
  - Call `start_join()` first in every most-derived session destructor, so
    callbacks see `joining` before derived members are destroyed.
    `start_join`'s `cleanup` argument may be empty.
- `node_request` (unary) and `node_request_stream` statuses: `OK`,
  `PARSE_ERROR` (bad json), `BAD_SELECTOR` (no name or type, or a stream
  request before any selector), `NOT_FOUND` (no matching node within 5 s, or a
  stream request naming a node other than the one the stream selected). A
  stream binds to the first node it selects; requests sent before that node
  exists are queued (up to 10) and answered `NOT_FOUND` after 5 s.

## Vulkan

- Thalamus requests a Vulkan 1.1 instance when the loader supports it, prefers
  a graphics queue family with compute support, and enables
  `VK_KHR_storage_buffer_storage_class` on 1.0 devices, for plugins that run
  compute shaders on the shared queue.
- macOS builds without a Vulkan SDK bundle the Vulkan loader and MoltenVK next
  to `native` (`cmake/vulkan.cmake`, `thalamus/build.py`).

## Testing tips

- A quick end-to-end check of the Python/native/.NET stack: run
  `python -m thalamus.pipeline -p <port> -u <ui port> -d <dotnet port>` in the
  background for ~20 s and inspect listening sockets and the native log
  (`~/thalamus_<timestamp>.log`; native's stdout prints its path).
- For TLS, a throwaway CA and certificates from `openssl` plus a small
  `grpc.aio` client calling `get_redirect` (or reflection) against each port
  exercise every server.
- Drive a running pipeline with `python -m thalamus.registry -p <jsonpath>
  [-s <json>]` (e.g. `-p '$.nodes[0].Running' -s true`, or `-p '$.nodes[1]'
  -s '{"name": "Node 2", "type": "NONE"}'` to add a node). `-p` alone prints
  the value. `--contrib` on `thalamus.pipeline` loads thalamus-contrib (put
  its `src` first on `PYTHONPATH` to use a development build).
- `--trace` (`-t` on `thalamus.pipeline`) records a Perfetto trace to
  `thalamus_<time>.perfetto-trace` in the working directory. The session
  flushes every second, so a crashed or killed Thalamus keeps all but the
  last second of every thread's events (without flushes, threads that trace
  rarely, like main, lost everything).
- Native crashes leave Crashpad minidumps in
  `~/thalamus_crashes/reports/`. lldb crashes loading `native.pdb`; instead
  read the exception address from the dump (parse its exception and module
  list streams) and symbolize `native.exe` offsets with `llvm-symbolizer
  --obj=native.exe <ImageBase + offset>` (ImageBase is `0x140000000`).
  Scanning the crashing thread's stack for addresses inside `native.exe` and
  plugins gives an approximate backtrace (stale frames included).
- `python -m thalamus.av_muxer -i <recording> -o out.mp4` muxes a recorded
  video node (MPEG4, H264 or VP9) and audio node (AAC or Opus) without
  re-encoding. It needs PyAV (`pip install av`, not a package dependency).
  Each video frame is placed at its record time (webcam frame rates wander,
  so a constant rate drifts by up to a second mid-recording even when both
  ends line up), and the audio by sample count. The ffmpeg CLI can't do
  this: with stream copy it times raw MPEG4 by the time base in the stream
  and ignores `-r`. Recordings can start mid-GOP; frames before the first
  keyframe are skipped.
  - The audio is placed from each record's time and `encoded_count` (see
    Analog data), whatever encoder produced it. Recordings from before
    thalamus-contrib timed encoded records that way (records without packets,
    `encoded_count` counting pushed samples) are rejected. The audio encoder
    usually started before the recording, so the first recorded packet isn't
    the stream's first.
  - Both tracks start at the first video frame, with earlier audio cut at
    the nearest packet boundary. Delaying a track with an edit list instead
    plays out of sync in players that ignore edit lists (Windows' Movies &
    TV), while ffplay and VLC honor them.
