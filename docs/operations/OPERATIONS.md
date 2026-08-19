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

The Python implementation remains the frontend-compatible reference while the
production operation is ported to C++20.

The current release-candidate operation emits
`unified3d.analysis-comparison/1.0-rc1`. It validates and embeds canonical
`unified3d.analysis/1.0-rc1` records, then evaluates deterministic evidence
levels 0–6: record validity, coordinate systems, bounds, mesh structure,
triangle statistics, vertex statistics and topology signatures. Level 7
requires spatial geometry access and therefore belongs in the native Runtime,
not in this record-only SDK operation.

The first native port is now implemented by `unified3d-operations` as
`compare_analysis_records()`. It consumes typed `AnalysisRecord` values from
`unified3d-core`, validates both inputs and reproduces the RC1 levels and
donor/target deductions. It does not parse JSON or expose RPC yet; those belong
to the Runtime gateway boundary.

### Prototype migration status

| Prototype node | Deterministic operation | Current headless exposure | Migration status |
|---|---|---|---|
| FBX Geometry Rig Analyzer | Analyze FBX geometry, rig and animation | Native Autodesk helper used by the private node | Must be registered behind the Unified3D Runtime and Python/TypeScript clients |
| GLB Geometry Rig Analyzer | Analyze GLB geometry, materials, rig and animation | glTF-Transform helper used by the private node | Must be registered behind the Unified3D Runtime and Python/TypeScript clients |
| Unified3D Analysis Comparator | Normalize and compare analysis records | `unified3d.compare_analyses()` and native `compare_analysis_records()` | Python reference and C++20 operation complete; JSON/RPC Runtime dispatch remains future work |
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
