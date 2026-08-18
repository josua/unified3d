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

### Windows

```text
Named Pipe
\\.\pipe\Unified3D.Runtime.v1
```

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

## 5. Initial minimum methods

```text
runtime.hello
runtime.shutdown
asset.load
asset.release
geometry.analyze
operation.status
operation.cancel
```

Planned methods include:

```text
asset.save
geometry.compare
skeleton.analyze
skeleton.compare
skin.transfer
animation.extract
animation.inject
validation.validate
```

## 6. Capability negotiation

A client begins with `runtime.hello` and receives independent version and capability information.

Example:

```json
{
  "runtime_version": "0.2.0",
  "protocol_version": "1.0",
  "capabilities": [
    "fbx.autodesk.read",
    "gltf.read",
    "gltf.write",
    "geometry.compare"
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
  "id": "asset:000014",
  "kind": "3D_ASSET",
  "format": "GLB"
}
```

The Runtime owns the native object. The frontend owns only the reference.

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

## 10. Security defaults

The Runtime is local-first and should default to:

```text
network access = disabled
resource policy = LocalOnly + Relative
force ref / unsafe overwrite semantics = disabled
```

Asset paths and external resources must be normalized and validated before access.
