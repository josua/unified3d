# Unified3D Python SDK

Headless Python API for Unified3D operations and, later, the native Runtime client.

This is the first executable Python SDK slice. It validates the
analysis-comparison contract observed through the FBX and GLB analyzer
prototypes and currently serves as the ComfyUI-compatible reference. The typed
model, validation and comparison operation now also exist in C++20; the next
step is routing this client through the native Runtime instead of executing the
reference operation locally.

## Current API

Canonicalize and validate an analyzer result without any UI dependency:

```python
from unified3d import canonicalize_analysis, validate_analysis

record = canonicalize_analysis(legacy_or_rc1_analysis)
validation = validate_analysis(record)
validation.raise_for_errors()

canonical_payload = record.to_dict()
```

Compare two analyses:

```python
from unified3d import compare_analyses

result = compare_analyses(fbx_analysis, glb_analysis)

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
0–6 (record validity through topology signatures). Missing evidence is reported
as `not_available` or `not_comparable`; it is never guessed.

The structured result and JSON serialization are UI-independent. Optional detailed and interpreted Markdown reports are available as plain strings:

```python
print(result.comparison_markdown)
print(result.interpreted_markdown)
```

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
- No file, network, asset, or GPU access.
- Deterministic comparison of small analysis records only.
- Typed, top-level immutable `AnalysisRecord` model with isolated source and dictionary exports.
- Structural and semantic RC1 validation with no runtime dependency.
- Conservative migration of the two existing prototype analyzer formats.
- The stable business result is the structured comparison data.
- Markdown is a report serializer, not the operation's primary result.
- The target architecture remains Python client → RPC/IPC → C++20 Runtime.

This package does not present the Python prototype as the final native comparison engine.
