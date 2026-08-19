# Unified Analysis Schema 1.0 RC1

**Identifier:** `unified3d.analysis/1.0-rc1`  
**Status:** Release candidate  
**Scope:** headless analyzer output shared by adapters, SDKs, Runtime and frontends

## Purpose

The contract removes format-dependent ambiguity before geometry transfer work
begins. It describes measured facts; it does not contain UI state and does not
claim compatibility that has not been computed.

The machine-readable source of truth is
[`schemas/unified3d.analysis-1.0-rc1.schema.json`](../../schemas/unified3d.analysis-1.0-rc1.schema.json).

## Required sections

```text
AnalysisRecord
├── analyzer
├── asset
│   └── coordinate_system
├── scene
├── geometry
├── materials
├── skeleton
├── skin
├── animation
├── native
└── diagnostics
```

All common sections and fields are present in every successful record. Native
format details remain under `native.fbx` or `native.gltf` and may evolve without
polluting the common model.

## Measurement-state rule

| Value | Meaning |
|---|---|
| `0` | Measured; the asset contains none |
| `null` | Not computed or not representable by this adapter |
| omitted | Invalid for common fields; allowed only in native extensions |

This distinction is normative. For example, an unskinned GLB reports
`skin.present=false` and `skin.skin_count=0`. A legacy FBX analysis that did not
measure scene count reports `scene.scene_count=null`.

## Deliberately distinct measurements

### Vertices

- `geometric_vertex_count` counts a geometric domain such as FBX control points
  or decoded unique position tuples.
- `geometric_vertex_semantic` states which domain was counted.
- `render_vertex_count` counts attribute tuples consumed by rendering.

These numbers must not be compared directly when their semantics differ.

### UVs

- `uv_channel_count` counts semantic channels such as `TEXCOORD_0`.
- `uv_set_binding_count` counts bindings observed across mesh resources.

The old FBX value `UV Sets: 10` migrates to `uv_set_binding_count=10`; the old
GLB value `UV Sets: 1` migrates to `uv_channel_count=1`.

### Materials

- `material_resource_count` counts distinct material resources.
- `material_binding_count` counts primitive or mesh bindings when measured.
- `texture_resource_count` counts distinct texture resources.

## Coordinates and bounds

Spatial comparison requires `handedness`, `up_axis`, `forward_axis` and
`meters_per_unit`. Unknown values remain `null` and produce a validation warning,
not a guessed default. Bounds are expressed in the declared asset coordinate
system until a later normalization operation produces a canonical-space record.

## Topology signatures

A topology signature contains:

- `algorithm` — stable algorithm identifier;
- `digest` — resulting digest;
- `domain` — exact data domain that was hashed.

Digests are comparable only when both `algorithm` and `domain` match. The
current FBX and GLB prototype signatures are adapter-local and therefore remain
diagnostic rather than proof of matching or different topology.

## Comparison levels 0–6

`unified3d.analysis-comparison/1.0-rc1` evaluates the evidence available in two
analysis records:

| Level | Evidence | Possible unresolved state |
|---:|---|---|
| 0 | Valid normalized records | — |
| 1 | Coordinate systems and units | `not_available` |
| 2 | Bounds | `not_available` |
| 3 | Mesh structure | `not_available` |
| 4 | Triangle statistics | `not_available` |
| 5 | Vertex statistics | `not_comparable` when semantics differ |
| 6 | Topology signatures | `not_comparable` when algorithms differ |

Each level returns `match`, `different`, `not_comparable` or `not_available`, an
optional score, and evidence. Overall `score` is computed only from measured
level scores; `coverage` reports how much evidence contributed. Spatial surface
correspondence begins at level 7 and requires geometry-buffer access, so it is
outside this analysis-record operation.

## Compatibility and migration

The Python SDK accepts the two prototype JSON formats and their text summaries.
`canonicalize_analysis()` migrates them conservatively to RC1.

```python
from unified3d import canonicalize_analysis, validate_analysis

record = canonicalize_analysis(legacy_analysis)
validation = validate_analysis(record)
validation.raise_for_errors()
payload = record.to_dict()
```

The migration layer is transitional. FBX and glTF analyzers must ultimately emit
RC1 directly; once both adapters and Runtime fixtures are stable, the final
`unified3d.analysis/1.0` identifier can be published.

## Native implementation status

The first C++20 conformance slice is available in `unified3d-core` and
`unified3d-operations`:

- typed in-memory representation of all RC1 sections;
- semantic validation with structured diagnostics;
- analysis-record comparison levels 0–6;
- regression parity for the `thief` FBX/GLB measurements under GCC and Clang.

JSON decoding is deliberately not implemented inside Core. The next boundary is
the native Runtime gateway, which will decode the wire record, construct the
typed Core value, invoke the operation and serialize its result.
