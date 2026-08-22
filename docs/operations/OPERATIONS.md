# Unified3D Operation Model

**Status:** Initial operation catalog derived from the v0.2 implementation specification.

## 1. Fundamental rule

```text
Operation != Node
```

An operation is deterministic business logic. A Workbench node, ComfyUI node, Python method or CLI command is only a frontend representation of that operation.

### Delivery rule for every node-backed feature

No new processing node is considered complete unless its behavior is available without its UI wrapper.

Required separation:

```text
Headless operation
├── structured inputs
├── structured result
├── deterministic diagnostics
└── SDK / Runtime exposure

Frontend adapter
├── socket and widget declaration
├── input/output conversion
└── optional presentation formatting
```

Forbidden in a frontend-only node implementation:

```text
analysis rules
comparison rules
transfer strategy selection
geometry mutation
validation policy
format conversion logic
```

Temporary prototypes must be extracted into a headless operation before they are treated as an SDK feature.

## 2. Operation contract

Conceptually, an operation exposes:

```text
Operation
├── id
├── version
├── input schema
├── output schema
├── parameter schema
├── execution policy
├── diagnostics
└── capabilities
```

Execution receives an `OperationContext` containing access to the asset registry, resource resolver, cache, cancellation token, progress reporter, thread pool, temporary storage, logger and security policy.

## 3. Categories

```text
Analysis
Comparison
Geometry
Materials
Rigging
Skinning
Animation
Conversion
Optimization
Validation
Serialization
Utility
```

## 4. MVP analysis operations

```text
AnalyzeAsset
AnalyzeGeometry
AnalyzeTopology
AnalyzeMaterials
AnalyzeSkeleton
AnalyzeSkin
AnalyzeAnimation
```

## 5. Comparison operations

```text
CompareAssets
CompareGeometry
CompareTopology
CompareSkeleton
CompareSkin
CompareMaterials
CompareAnimations
```

### Initial executable analysis-record comparison

The first Python SDK slice exposes a deterministic comparison of existing FBX/GLB analysis records:

```python
from unified3d import compare_analyses

result = compare_analyses(analysis_a, analysis_b)

structured = result.to_dict()
serialized = result.to_json()
```

Operation characteristics:

- accepts dictionaries, JSON strings, or supported analyzer summaries;
- normalizes adapter-specific fields conservatively;
- returns structured comparison facts as the primary result;
- exposes Markdown only as an optional report serialization;
- has no ComfyUI or renderer dependency;
- performs no asset-file, network, GPU, or geometry-buffer access.

Two GLB records are supported directly. Their adapter-local topology
signatures are comparable when the algorithm and signature domain match, and
the interpreted report distinguishes identical topology from expected
topology reconstruction after decimation. Optional texture-image inventories
compare resolution classes and encoded image weight.

The Python implementation remains the frontend-compatible reference while the
production operation is ported to C++20.

The current release-candidate operation emits
`unified3d.analysis-comparison/1.0-rc1`. It validates and embeds canonical
`unified3d.analysis/1.0-rc1` records, then evaluates deterministic evidence
levels 0–7: record validity, coordinate systems, raw bounds, mesh structure,
triangle statistics, vertex statistics, topology signatures and canonical
spatial bounds. Level 7 converts the eight AABB corners through declared axes,
handedness and `meters_per_unit`, then evaluates extent similarity, normalized
center distance and AABB IoU. It is a conservative metadata alignment gate;
surface correspondence still requires decoded geometry buffers.

### Initial executable spatial skin transfer

The first native `skin.transfer` slice now implements the prescribed
surface-based transfer path:

```text
target render vertex
→ closest donor triangle (BVH)
→ barycentric coordinates
→ interpolate all donor JOINTS_n / WEIGHTS_n sets
→ merge identical joints
→ threshold and deterministic pruning
→ normalize surviving weights
→ register target-owned Runtime resources
```

The operation works in the canonical metric world frame produced by the native
FBX/glTF adapters and applies every primitive's `local_to_world` transform. A
maximum metric distance rejects unsafe projections. The Python SDK exchanges
only typed asset handles and a compact report; geometry and skin arrays remain
inside the Runtime.

This operation deliberately stops at weight-buffer production. Skeleton
hierarchy, inverse bind matrices, animation injection and format serialization
must be implemented and validated separately before a transferred character
can be written as an animated GLB.

The first native port is now implemented by `unified3d-operations` as
`compare_analysis_records()`. It consumes typed `AnalysisRecord` values from
`unified3d-core`, validates both inputs and reproduces the RC1 levels and
donor/target deductions. JSON decoding and RPC remain at the Runtime gateway
boundary. The Python SDK calls that Runtime through stdio or Windows Named Pipe
while retaining the local Python implementation as its conformance oracle.

### Prototype migration status

| Prototype node | Deterministic operation | Current headless exposure | Migration status |
|---|---|---|---|
| FBX Geometry Rig Analyzer | Analyze FBX geometry, rig and animation | Native Autodesk helper used by the private node | Must be registered behind the Unified3D Runtime and Python/TypeScript clients |
| GLB Geometry Rig Analyzer | Analyze GLB geometry, materials, rig and animation | glTF-Transform helper used by the private node | Must be registered behind the Unified3D Runtime and Python/TypeScript clients |
| Unified3D Analysis Comparator | Normalize and compare analysis records | Python oracle, native `compare_analysis_records()` and Runtime RPC client | Levels 0–7 and both local transports implemented |
| Spatial Skin Transfer | Closest-surface mapping and normalized weight interpolation | Native C++20 Core, Runtime RPC and typed Python client | Weight buffers implemented; skeleton/bind/animation serialization remains |
| Markdown Input Preview | Render a string as Markdown | Not applicable | Presentation-only node; no business operation belongs in the SDK |

The presentation-only exception is narrow: a node may remain frontend-only when its complete purpose is rendering, layout, interaction or visualization and it contains no processing decision that changes an asset or structured business result.

## 6. Geometry compatibility classes

```text
EXACT_TOPOLOGY_MATCH
DIRECT_SKIN_TRANSFER_COMPATIBLE
GEOMETRY_MATCH
GEOMETRIC_VERTEX_MAPPING_REQUIRED
SPATIAL_MATCH
SPATIAL_SKIN_TRANSFER_REQUIRED
SIMILAR_GEOMETRY
ADVANCED_TRANSFER_REQUIRED
INCOMPATIBLE
```

A comparison result may expose a score in the range `0.0 → 1.0` together with component scores such as bounds, triangle, topology, spatial and skeleton scores.

## 7. Geometry normalization

The working comparison representation may be normalized through:

```text
clone working representation
→ apply node transforms
→ convert coordinate system
→ convert units
→ normalize winding
→ canonicalize vertex positions
→ canonicalize topology
```

The source asset remains unchanged.

## 8. Skin transfer modes

### Direct

Use when topology, geometric vertex mapping and skeleton compatibility are sufficient.

### Geometric vertex mapping

Use when render-vertex counts differ because seams, hard normals or material boundaries duplicate render vertices while the geometric surface remains equivalent.

### Spatial surface transfer

Recommended conceptual pipeline:

```text
target vertex
→ nearest source triangle
→ barycentric coordinates
→ interpolate source skin weights
→ normalize influences
```

Quality levels reserved by the v0.2 specification:

```text
Fast
Balanced
Precise
Diagnostic
```

## 9. Animation operations

```text
Animation Extract
Animation Inject
Animation Validate
```

Injection validates skeleton hierarchy, joint mapping, rest pose, target properties and time base before writing animation data.

## 10. Founding workflow operation graph

```text
Load Original GLB
      │
      ├───────────────┐
      ▼               ▼
Analyze GLB      Load Rigged FBX
                      │
                      ▼
                 Analyze FBX
                      │
      ┌───────────────┘
      ▼
Compare Geometry
      ↓
Compare Skeleton
      ↓
Transfer Skin
      ↓
Transfer Animation
      ↓
Validate GLB
      ↓
Save Final GLB
```

## 11. Execution model

Long operations may execute through the Runtime scheduler with dependency resolution, parallel scheduling, cache reuse, cancellation, error propagation and progress aggregation.
