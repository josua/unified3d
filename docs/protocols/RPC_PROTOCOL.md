# Unified3D RPC / IPC Protocol

**Protocol status:** Initial draft  
**Protocol family:** JSON-RPC 2.0 control plane over local IPC

## 1. Purpose

The protocol connects frontend processes to the persistent native `unified3d-runtime` without moving large 3D buffers into UI processes.

```text
Workbench / ComfyUI / CLI
        ↓
Client SDK
        ↓
JSON-RPC 2.0
        ↓
IPC Transport
        ↓
unified3d-runtime
```

## 2. Initial transports

### Implemented stdio transport

The first executable slice uses newline-delimited JSON-RPC 2.0 over standard
input/output. Each non-empty line is one complete request. Each response is one
complete line and is flushed immediately. This transport exists to validate the
wire contract and client SDKs without coupling the protocol to the final IPC
implementation.

### Implemented local transport on Windows

```text
Named Pipe
\\.\pipe\Unified3D.Runtime.v1
```

Start it with `unified3d-runtime --pipe` or pass a custom local pipe name as
the following argument. It uses the same newline framing and dispatcher as
stdio. Remote clients are rejected. The pipe ACL grants full access to the
creating owner, administrators and Local System; the initial implementation is
synchronous and accepts one connected client at a time.

### Linux / macOS

```text
Unix Domain Socket
```

The exact secure per-user socket location remains an implementation detail to finalize.

## 3. Control plane versus data plane

JSON-RPC transports:

- method names;
- parameters;
- handles;
- small analytical results;
- diagnostics;
- progress and state events.

JSON-RPC must not transport:

- millions of vertices;
- full texture payloads;
- very large animation buffers;
- gigabyte-scale asset data.

Future large-data transfer may use shared memory, memory-mapped files, binary IPC streams or temporary binary resources.

## 4. Initial namespaces

```text
runtime.*
session.*
asset.*
resource.*
geometry.*
material.*
skeleton.*
skin.*
animation.*
validation.*
conversion.*
operation.*
cache.*
```

## 5. Implemented methods

```text
runtime.hello
runtime.shutdown
analysis.validate
analysis.compare
asset.load
asset.release
```

The Runtime implements JSON-RPC notifications: a request without `id` executes
but emits no response. Batch requests are not part of the initial slice.

### `runtime.hello`

Returns independent Runtime, Core, protocol and schema versions plus the exact
capability list. It requires no parameters.

```json
{"jsonrpc":"2.0","id":1,"method":"runtime.hello"}
```

### `analysis.validate`

Validates a canonical `unified3d.analysis/1.0-rc1` object structurally and
semantically. A well-formed request always returns `result.valid`; validation
diagnostics are machine-readable and include a JSON path.

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "analysis.validate",
  "params": { "analysis": { "...": "canonical RC1 analysis" } }
}
```

### `analysis.compare`

Validates two canonical analyses, executes the deterministic native comparison,
and returns `unified3d.analysis-comparison/1.0-rc1`. Evidence values retain
their wire type: counts and scores are numbers, predicates are booleans and
reasons are strings.

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "analysis.compare",
  "params": {
    "a": { "...": "canonical analysis A" },
    "b": { "...": "canonical analysis B" }
  }
}
```

Level 7 normalizes known axes, handedness, unit scale and available geometry
(or scene) bounds into a right-handed, Y-up, −Z-forward metric frame. This is a
metadata-level alignment result, not proof of vertex or surface correspondence.

### `asset.load`

Registers an existing `.fbx`, `.glb` or `.gltf` source in the Runtime registry.
The selected native adapter decodes triangle geometry and optional skin
influences before the Runtime registers any child resource.

```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "method": "asset.load",
  "params": {
    "path": "C:/assets/character.glb",
    "backend": "auto"
  }
}
```

The result contains `asset` and `reused`. Loading the same unchanged live file
increments its retain count and returns the same handle. `asset.primitives`
contains only lightweight descriptors for `VERTEX_BUFFER`, `INDEX_BUFFER` and
`SKIN_WEIGHT_BUFFER` resources. Descriptors expose scalar type, element count,
byte length, generation and provenance, but never inline buffer bytes.

Each primitive also declares:

```text
domain                 GEOMETRIC_VERTICES or RENDER_VERTICES
local_to_world         16 column-major float64 values
max_influences         maximum non-zero influences on one vertex
influence_sets         ordered JOINTS_n/WEIGHTS_n resource pairs
```

All current native adapters normalize world space to
`RIGHT_HANDED_Y_UP` with `buffer_unit_meters = 1.0`. FBX positions remain
geometric control points; glTF positions remain render vertices.

### `asset.release`

Releases one retained reference. A final release destroys the asset and all
owned geometry/skin resources. Future slot reuse increments each generation,
so old parent and child handles become stale.

```json
{
  "jsonrpc": "2.0",
  "id": 5,
  "method": "asset.release",
  "params": {
    "asset": {
      "id": "asset:session-id:1:14",
      "kind": "3D_ASSET",
      "session": "session-id",
      "generation": 1,
      "object_id": 14
    }
  }
}
```

### Planned methods

```text
asset.save
geometry.compare
skeleton.analyze
skeleton.compare
skin.transfer
animation.extract
animation.inject
validation.validate
operation.status
operation.cancel
```

## 6. Capability negotiation

A client begins with `runtime.hello` and receives independent version and capability information.

Example:

```json
{
  "runtime_version": "0.2.0-dev",
  "core_version": "0.2.0-dev",
  "protocol_version": "1.0",
  "analysis_schema": "unified3d.analysis/1.0-rc1",
  "analysis_comparison_schema": "unified3d.analysis-comparison/1.0-rc1",
  "capabilities": [
    "transport.stdio",
    "analysis.validate",
    "analysis.compare"
  ]
}
```

Version domains remain distinct:

```text
Core Version
Runtime Version
RPC Protocol Version
Schema Version
Adapter Version
Operation Version
```

## 7. Handle model

Frontends receive lightweight typed references such as:

```text
AssetHandle
SceneHandle
NodeHandle
MeshHandle
PrimitiveHandle
MaterialHandle
TextureHandle
VertexBufferHandle
IndexBufferHandle
SkinWeightBufferHandle
SkeletonHandle
SkinHandle
AnimationHandle
AnalysisHandle
ComparisonHandle
ValidationHandle
OperationHandle
```

Example:

```json
{
  "id": "asset:session-id:3:14",
  "kind": "3D_ASSET",
  "session": "session-id",
  "generation": 3,
  "object_id": 14,
  "format": "GLTF",
  "container": "GLB",
  "provenance": {
    "producer": "asset.load",
    "operation_id": "load:session-id:3:14",
    "source_uri": "C:/assets/character.glb",
    "source_revision": "runtime-observed-revision",
    "parents": []
  }
}
```

The Runtime owns the resource. The frontend owns only a typed reference. A
handle is valid only for its originating Runtime session, object type, object
identifier and current generation. Provenance follows the resource and records
its producer, operation, source revision and parent handles.

## 8. Long-running operations

Long operations return an `OperationHandle` and emit progress notifications.

States:

```text
Queued
Running
Completed
Failed
Cancelled
```

Example progress event:

```json
{
  "jsonrpc": "2.0",
  "method": "operation.progress",
  "params": {
    "operation": "operation:000291",
    "progress": 0.46,
    "phase": "Spatial Vertex Mapping"
  }
}
```

## 9. Error model

Canonical categories:

```text
ProtocolError
InvalidRequest
InvalidHandle
AssetError
AdapterError
OperationError
ValidationError
ResourceError
InternalError
```

Errors should expose a stable machine-readable code, a human-readable message and structured details when available.

The initial dispatcher uses the standard JSON-RPC errors and two operation
errors:

| Code | Meaning |
|---:|---|
| `-32700` | Parse error |
| `-32600` | Invalid Request |
| `-32601` | Method not found |
| `-32602` | Invalid params |
| `-32603` | Internal error |
| `-32001` | Control message exceeds the 4 MiB limit |
| `-32010` | Analysis validation failed before comparison |
| `-32011` | Native analysis comparison failed |
| `-32020` | Invalid, foreign or stale asset handle |
| `-32021` | Asset registration failed |

The 4 MiB ceiling applies to each stdio control message. This is a defensive
limit and a design constraint: heavy 3D data belongs to the data plane.

## 10. Security defaults

The Runtime is local-first and should default to:

```text
network access = disabled
resource policy = LocalOnly + Relative
force ref / unsafe overwrite semantics = disabled
```

Asset paths and external resources must be normalized and validated before access.
