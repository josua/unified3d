# Unified3D Operation Model

**Status:** Initial operation catalog derived from the v0.2 implementation specification.

## 1. Fundamental rule

```text
Operation != Node
```

An operation is deterministic business logic. A Workbench node, ComfyUI node, Python method or CLI command is only a frontend representation of that operation.

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
