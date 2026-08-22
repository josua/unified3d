# Unified3D Format Adapter Model

**Status:** Native geometry/skin buffer production implemented for FBX and glTF.

## 1. Purpose

Format adapters translate native 3D formats into and out of the Unified3D Core object model.

```text
Native 3D Asset
↕
Format Adapter
↕
Unified3D Core Asset
```

The Core does not clone the Autodesk FBX SDK API, the glTF JSON schema or the USD API. It represents common semantics required by Unified3D operations.

## 2. Core rule

> Normalize what is semantically common. Preserve what is format-specific.

Format-specific information is retained as native extension data instead of contaminating common abstractions.

Examples:

```text
FBX-specific
Control Points / Clusters / Deformers / Layers

glTF-specific
Accessors / BufferViews / Extensions / Primitives

USD-specific
Prims / Schemas / Metadata
```

## 3. Adapter capabilities

An adapter may advertise capabilities such as:

```text
Read
Write
Mesh
Materials
Textures
Skeleton
Skin
Animation
MorphTargets
Cameras
Lights
CustomProperties
EmbeddedResources
```

## 4. Initial adapters

### Autodesk FBX Adapter

Optional reference backend using a separately installed Autodesk FBX SDK.

The native implementation imports the scene with FBX SDK 2020.3.10, converts
axes to right-handed Y-up, converts units to meters, triangulates mesh polygons,
and emits control-point positions, triangle indices and all cluster-derived skin
influences without truncating the source maximum. Its output is registered by
the Runtime under the same immutable resource contract as the portable ufbx
backend.

The Autodesk-enabled build also provides a guarded GLB-to-FBX writer for
unrigged assets. It creates a binary FBX without decimation or vertex welding,
preserves triangle topology, normals, UVs, transforms and PBR texture
assignments, and can embed all source images. A completed export is reimported
through the Autodesk adapter so invalid output does not enter the Runtime
registry. Skins, animation, morph targets and compressed primitives are
currently refused rather than silently omitted.

The public repository may contain:

```text
Unified3D adapter source
build detection
CMake integration
runtime interface
documentation
```

It must not contain redistributed Autodesk SDK source, headers, libraries or DLLs.

### ufbx Adapter

Open-source FBX backend intended to provide FBX functionality without a proprietary runtime dependency.

The implementation is pinned to ufbx 0.23.0. It emits float64 control-point
positions, uint32 triangle indices, all non-zero cluster influences split into
four-lane `JOINTS_n/WEIGHTS_n` resources, and per-instance geometry-to-world
matrices. Weights are merged per joint and normalized without truncation.

### glTF / GLB Adapter

The v0.2 specification allows an MVP path using Node.js + `@gltf-transform/core` as specialized tooling while explicitly keeping the C++ Core independent of Node.js.

The native backend is now implemented with pinned cgltf 1.15. It reads `.gltf`
and `.glb`, expands triangle strips/fans, emits float32 render positions and
uint32 triangle indices, and preserves every contiguous skin influence set.
Draco and Meshopt primitives are rejected with explicit diagnostics until a
decoder is configured; compressed bytes are never mistaken for decoded data.

### USD / USDZ Adapter

Reserved for a future phase and not part of the initial MVP.

## 5. Cross-backend validation

The same FBX asset may be loaded by both Autodesk and ufbx backends and compared after translation into Unified3D representations.

```text
Same FBX
├── AutodeskFbxAdapter
└── UfbxAdapter
        ↓
Unified Assets
        ↓
Backend Consistency Comparator
```

This is intended to make backend differences observable and testable.

The current real-file regression gives identical Autodesk/ufbx structural
results for `thief-walking.fbx`: 10 mesh instances, 27,311 control points,
50,059 triangles and six maximum influences.

## 6. Geometry representation requirement

Adapters must preserve the distinction between:

```text
Geometric Vertex
and
Render Vertex
```

This distinction is required because UV seams, hard normals and material boundaries can duplicate render vertices without changing the underlying surface.

A `VertexMapping` can therefore map one geometric vertex to multiple render vertices.

Native FBX and glTF buffers now feed a common closest-triangle BVH and
barycentric skin-weight interpolation operation. The result is registered as
target-owned `JOINTS_n/WEIGHTS_n` resources with donor and target provenance.
This proves the shared geometry-buffer contract without pretending that a
geometric match implies identical vertex order or connectivity.

The current transfer slice does not yet serialize the new buffers or construct
a target skeleton/bind-pose graph. Format writers remain responsible for
mapping the shared semantic result back to valid FBX, glTF or future USD data.
