# Unified3D Runtime

`unified3d-runtime` is the persistent C++20 process that owns native state and
dispatches deterministic operations. The same newline-delimited JSON-RPC 2.0
dispatcher is exposed over standard input/output and a local Windows Named Pipe.

Initial methods:

```text
runtime.hello
runtime.shutdown
analysis.validate
analysis.compare
asset.load
asset.release
```

The Runtime uses `nlohmann/json` only at this wire boundary. Core and Operations
remain JSON-independent. Large geometry, texture and animation buffers are
owned by the Runtime and never carried by this control-plane transport.

The dependency is pinned to `nlohmann/json` 3.12.0 and verified with the
upstream release archive SHA-256 during CMake configuration. Implicit JSON
conversions are disabled.

`asset.load` selects a native adapter, decodes an existing FBX/glTF source and
registers its immutable position, triangle-index and optional skin-influence
buffers. The result contains typed generational descriptors with provenance;
the bytes remain private to the Runtime. Loading the same unchanged live source
retains and reuses its handle. A final `asset.release` cascades to every child
buffer and makes all former generations stale.

Supported `backend` values are `auto`, `cgltf`, `ufbx` and `autodesk_fbx`.
Automatic FBX selection prefers Autodesk when that optional backend was built,
and otherwise uses ufbx. All decoded buffers are expressed in a right-handed,
Y-up metric world frame; every primitive retains its local-to-world matrix and
its geometric-vs-render-vertex domain.

## Run the development transports

Build with a repository preset, then start:

```powershell
.\build\dev-gcc\runtime\unified3d-runtime.exe
# Equivalent explicit form:
.\build\dev-gcc\runtime\unified3d-runtime.exe --stdio

# Windows Named Pipe (default name shown):
.\build\dev-gcc\runtime\unified3d-runtime.exe --pipe \\.\pipe\Unified3D.Runtime.v1
```

Send one compact JSON-RPC request per line. The process stays alive until EOF or
`runtime.shutdown`. Empty lines are ignored and every response is flushed as a
single line. A control message is limited to 4 MiB; asset buffers must use the
native resource registry rather than JSON.

Example request:

```json
{"jsonrpc":"2.0","id":1,"method":"runtime.hello"}
```

The Named Pipe rejects remote clients and is created with an explicit ACL for
the owner, administrators and Local System. The current transport deliberately
serves one connected client at a time; concurrency and discovery are later
protocol milestones.
