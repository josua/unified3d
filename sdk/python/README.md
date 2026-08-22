# Unified3D Python SDK

Headless Python API for Unified3D operations and the native Runtime client.

This is the first executable Python SDK slice. It validates the
analysis-comparison contract observed through the FBX and GLB analyzer
prototypes and currently serves as the ComfyUI-compatible reference. The typed
model, validation and comparison operation also exist in C++20. The local
Python operation remains the explicit conformance oracle, while production
callers can route through the persistent native Runtime.

## Current API

Canonicalize and validate an analyzer result without any UI dependency:

```python
from unified3d import canonicalize_analysis, validate_analysis

record = canonicalize_analysis(legacy_or_rc1_analysis)
validation = validate_analysis(record)
validation.raise_for_errors()

canonical_payload = record.to_dict()
```

Compare locally with the oracle:

```python
from unified3d import compare_analyses_oracle

result = compare_analyses_oracle(fbx_analysis, glb_analysis)

print(result.comparison["same_mesh_count"])
print(result.comparison["compatibility"])
print(result.to_json())
```

Inputs may be:

- Python dictionaries containing analyzer JSON;
- serialized analyzer JSON strings;
- supported FBX or GLB text summaries.

Comparison JSON uses `unified3d.analysis-comparison/1.0-rc1` and embeds canonical
`unified3d.analysis/1.0-rc1` inputs. The compatibility result evaluates levels
0–7 (record validity through metadata-level spatial alignment). Missing evidence is reported
as `not_available` or `not_comparable`; it is never guessed.

The structured result and JSON serialization are UI-independent. Optional detailed and interpreted Markdown reports are available as plain strings:

```python
print(result.comparison_markdown)
print(result.interpreted_markdown)
```

When both inputs are GLB analyses, `interpreted_markdown` orders the denser
asset as the source and emits a dedicated decimation report. GLB Analyzer
0.3.0 texture inventories add per-resolution image counts and encoded image
weight, allowing 4K/2K/1K and per-channel downscaling to be compared without
inferring resolution from the container size.

Call the native Runtime without any UI dependency:

```python
from unified3d import Unified3DClient

client = Unified3DClient.connect_named_pipe()
try:
    hello = client.hello()
    comparison = client.compare_analyses(fbx_analysis, glb_analysis)
    loaded = client.load_asset(r"C:\assets\character.glb", backend="cgltf")
    print(loaded.asset.adapter)
    for primitive in loaded.asset.primitives:
        print(
            primitive.domain,
            primitive.positions.element_count,
            primitive.indices.element_count if primitive.indices else 0,
            primitive.max_influences,
        )
    client.release_asset(loaded.asset)
finally:
    client.close()
```

For tests or process ownership, use
`Unified3DClient.connect_stdio(runtime_executable)`. `shutdown()` stops the
Runtime; `close()` only releases the client transport.

Convert an unrigged GLB to a self-contained FBX without decimation:

```python
from unified3d import Unified3DClient

client = Unified3DClient.connect_stdio(
    r"C:\path\to\unified3d-runtime.exe"
)
try:
    source = client.load_asset(r"C:\assets\character.glb", backend="cgltf")
    result = client.convert_glb_to_fbx(
        source.asset,
        r"C:\assets\character_lossless_embedded.fbx",
        embed_media=True,
        overwrite=False,
    )
    assert result.report.geometry_preserved
    assert result.report.media_embedded
    print(result.report.triangle_count)
    print(result.report.embedded_media_count)
finally:
    client.shutdown()
```

This writer is available only when Unified3D is built with Autodesk FBX SDK
2020.3.10. It deliberately rejects GLBs containing a skin, animation, morph
target, Draco or Meshopt data; these features are not discarded implicitly.

Normalize a guarded mixed-unit rig into a new GLB:

```python
from unified3d import Unified3DClient

client = Unified3DClient.connect_stdio(
    r"C:\path\to\unified3d-runtime.exe"
)
try:
    source = client.load_asset(r"C:\assets\character.glb", backend="cgltf")
    result = client.normalize_spatial(
        source.asset,
        r"C:\assets\character_unified3d_normalized.glb",
        expected_position_height_m=1.70,
        height_tolerance_m=0.05,
        correct_scale_factor=True,
        remove_emissive_channel=True,
        remove_head_helper_bones=True,
        remove_animations=True,
    )
    assert (
        result.source_asset.canonical_geometry_fingerprint.digest
        == result.normalized_asset.canonical_geometry_fingerprint.digest
    )
    print(result.report.absorbed_uniform_scale)
    print(result.report.removed_emissive_texture_count)
    print(result.report.zeroed_emissive_factor_count)
    print(result.report.scale_correction_applied)
    print(result.report.emissive_correction_applied)
    print(result.report.removed_head_helper_joint_count)
    print(result.report.removed_animation_clip_count)
finally:
    client.shutdown()
```

The four corrections are independently selectable, but at least one must remain
enabled. The operation is executed by the C++ Runtime. Emissive correction
removes material `emissiveTexture` references and writes explicit zero
`emissiveFactor` values without deleting shared image resources.
`remove_head_helper_bones` removes the zero-weight Meshy `head_end` and
`headfront` nodes from the hierarchy, skin, inverse bind matrices and animation
channels. `remove_animations` removes every clip while preserving the mesh,
skin weights and retained joints. The expected height is a scale-safety gate
rather than a target resize height. Python receives
typed handles and a lightweight report only; no geometry buffer is copied
through JSON-RPC.

Transfer donor skin weights to a distinct target surface:

```python
from unified3d import Unified3DClient

client = Unified3DClient.connect_named_pipe()
try:
    donor = client.load_asset(r"C:\assets\animated.fbx", backend="autodesk_fbx")
    target = client.load_asset(r"C:\assets\high_definition.glb", backend="cgltf")
    transfer = client.transfer_skin(
        donor.asset,
        target.asset,
        quality="diagnostic",
        maximum_influences=4,
        maximum_distance_m=0.05,
        replace_existing=False,
    )
    print(transfer.report.matched_vertex_count)
    print(transfer.report.mean_distance_m)
    for primitive in transfer.target_asset.primitives:
        print(primitive.max_influences, primitive.influence_sets)
finally:
    client.shutdown()
```

The target handle is updated with Runtime-owned transferred weight resources.
This call does not write a file and does not yet inject the donor skeleton,
inverse bind matrices or animations.

## Development installation

```powershell
python -m pip install --no-deps -e sdk/python
```

## Test

```powershell
python -m unittest discover -s sdk/python/tests -v
```

## Architectural status

- No ComfyUI dependency.
- No frontend or renderer dependency.
- Local oracle has no file, network, asset, or GPU access.
- Runtime client supports analysis validation/comparison, native asset decoding,
  typed buffer descriptors, canonical geometry fingerprints, guarded GLB
  spatial normalization, lossless unrigged GLB-to-FBX conversion with embedded
  media, spatial skin-weight transfer and cascading asset lifetime RPC.
- Typed, top-level immutable `AnalysisRecord` model with isolated source and dictionary exports.
- Structural and semantic RC1 validation with no runtime dependency.
- Conservative migration of the two existing prototype analyzer formats.
- The stable business result is the structured comparison data.
- Markdown is a report serializer, not the operation's primary result.
- Stdio and Windows Named Pipe clients use the same synchronous typed facade.

This package does not present the Python prototype as the final native comparison engine.
