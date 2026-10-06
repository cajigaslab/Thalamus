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
    `thalamus_get_node_version` and `thalamus_get_analog_node_version`: the
    number of pointers after `ThalamusNodeFactory::plugin_impl`,
    `ThalamusNode::signals_offmain` and `ThalamusAnalogNode::name`
    respectively. Missing exports mean 0.
  - For structs Thalamus provides to plugins (e.g. `ThalamusAnalogNode` for
    C++ nodes), the host reports its version through an API function
    (`ThalamusAPI::analog_node_version`), so plugins can check it.
- Spans returned through the API are borrowed and only valid during the
  `ready`/`get_node` callback.
- A node with a modality doesn't have that data in every message (an image
  node can emit audio-only messages and vice versa). Check
  `has_image_data`/`has_analog_data` before reading; e.g. `channel_info`
  calls `num_channels()` without checking, which is only safe because
  plugins keep analog data on every message.
- thalamus-contrib vendors `plugin.h` and `modalities.h` in
  `rust/include/thalamus/`; copy them over after changing them here.

## State

- `State::connect`-style callbacks (`state_recursive_change_connect`) fire for
  changes anywhere below the connected collection, not just its own keys.
  Filter on the source collection when only the node's own keys matter.
- `ThalamusState*` wrappers are interned per collection (`get_state_ref`), so
  two wrappers of the same live collection compare equal by pointer.
- Setting state inside a state callback re-enters callbacks synchronously.
- `thalamus.pipeline` sets every node's `Running` to false when it loads a
  config; nodes are started explicitly (UI or `thalamus.registry`).

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
- Native crashes leave Crashpad minidumps in
  `~/thalamus_crashes/reports/`. lldb crashes loading `native.pdb`; instead
  read the exception address from the dump (parse its exception and module
  list streams) and symbolize `native.exe` offsets with `llvm-symbolizer
  --obj=native.exe <ImageBase + offset>` (ImageBase is `0x140000000`).
  Scanning the crashing thread's stack for addresses inside `native.exe` and
  plugins gives an approximate backtrace (stale frames included).
- `python -m thalamus.av_muxer -i <recording> -o out.mp4` muxes a recorded
  MPEG4 video node and AAC audio node without re-encoding. It needs PyAV
  (`pip install av`, not a package dependency). Each video frame is placed
  at its record time (webcam frame rates wander, so a constant rate drifts
  by up to a second mid-recording even when both ends line up), and the
  audio by sample count from its first record time, AAC priming
  compensated. The ffmpeg CLI can't do this: with stream copy it times raw
  MPEG4 by the time base in the stream and ignores `-r`. Recordings can
  start mid-GOP; frames before the first MPEG4 VOL header are skipped.
