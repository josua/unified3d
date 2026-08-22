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
asset.convert_glb_to_fbx
asset.normalize_spatial
asset.release
skin.transfer
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

`asset.convert_glb_to_fbx` uses the optional Autodesk FBX SDK writer to convert
an unrigged GLB into a native binary FBX without decimation. It preserves every
triangle and render vertex as an FBX control point, recreates normals, UVs,
node transforms and materials, and can embed every GLB image in the FBX. The
operation refuses skins, animations, morph targets and compressed primitives
instead of silently dropping unsupported data. The exported FBX is immediately
reimported through the Autodesk adapter before the Runtime returns it.

`asset.normalize_spatial` writes a new self-contained GLB for the guarded case
where metric POSITION data is hidden below a power-of-ten rig root scale. It
converts joint and animation translations, scales the first 15 affine
components of every inverse bind matrix while preserving its final homogeneous
component and resets the root scale. Independently selectable Meshy cleanup can
remove material `emissiveTexture` references, write an explicit zero
`emissiveFactor`, remove the zero-weight `head_end`/`headfront` helper joints,
and remove every animation clip while retaining the skinned mesh and rig. Image
and texture resources are preserved so a shared base-color reference remains valid. The result is validated through
cgltf and registered as a Runtime-owned asset with source provenance. A
canonical triangle-position fingerprint proves that the operation did not
modify the triangle geometry.

Callers select `correct_scale_factor`, `remove_emissive_channel`,
`remove_head_helper_bones`, and `remove_animations` independently; at least one
must remain enabled. Helper removal refuses any `head_end` or `headfront` joint
with a non-zero skin weight, remaps JOINTS accessors and inverse bind matrices,
physically removes both nodes, and prunes their animation channels. Animation
removal erases all clips without removing skin weights or the joint hierarchy.
The expected POSITION height is validated only for scale correction and is not
a target resize height.

`skin.transfer` resolves two live assets entirely inside the registry, projects
each target render vertex onto the closest donor triangle, interpolates and
normalizes the donor weights, then registers new target-owned skin resources.
The control response contains handles and distance statistics only. The source
and target files are unchanged; skeleton/bind-pose construction, animation
injection and serialization are subsequent operations.

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
