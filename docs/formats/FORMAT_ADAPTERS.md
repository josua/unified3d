# Unified3D Format Adapter Model

**Status:** Initial adapter contract summary derived from the v0.2 architecture.

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

### glTF / GLB Adapter

The v0.2 specification allows an MVP path using Node.js + `@gltf-transform/core` as specialized tooling while explicitly keeping the C++ Core independent of Node.js.

A native C++ glTF backend remains a future implementation option.

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

## 6. Geometry representation requirement

Adapters must preserve the distinction between:

```text
Geometric Vertex
and
Render Vertex
```

This distinction is required because UV seams, hard normals and material boundaries can duplicate render vertices without changing the underlying surface.

A `VertexMapping` can therefore map one geometric vertex to multiple render vertices.
