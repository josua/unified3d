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
  typed buffer descriptors and cascading asset lifetime RPC.
- Typed, top-level immutable `AnalysisRecord` model with isolated source and dictionary exports.
- Structural and semantic RC1 validation with no runtime dependency.
- Conservative migration of the two existing prototype analyzer formats.
- The stable business result is the structured comparison data.
- Markdown is a report serializer, not the operation's primary result.
- Stdio and Windows Named Pipe clients use the same synchronous typed facade.

This package does not present the Python prototype as the final native comparison engine.
