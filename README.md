# Unified3D

> **Headless 3D Asset Processing Framework**  
> Analyze, compare, transform, rig, animate, validate and convert 3D assets without requiring a DCC, viewport or renderer.

**Status:** Early implementation / operation-contract validation phase

**Architecture:** C++20 Core + persistent native Runtime  
**Frontends:** TypeScript Workbench, Python/ComfyUI, CLI  
**Initial formats:** FBX, GLB/glTF  
**Future formats:** USD/USDZ and additional interchange formats

## Project goal

Unified3D separates **3D data processing** from **3D visualization**.

```text
Traditional DCC workflow
Open application
→ Initialize UI / viewport
→ Load scene
→ Configure tools
→ Process asset
→ Export

Unified3D workflow
Load
→ Process
→ Validate
→ Save
```

The target is a reusable processing infrastructure for deterministic 3D operations that can run locally, headlessly, in batch environments, or behind node-based frontends.

## Core architectural rules

- `Rendering != Processing`
- `Operation != Node`
- Every frontend node must delegate deterministic business logic to a headless operation exposed through an SDK or the Runtime.
- UI formatting and widgets may live in a frontend wrapper; analysis, comparison and transformation rules may not.
- Core and Runtime target C++20.
- Workbench is a TypeScript frontend.
- ComfyUI is a Python frontend.
- Frontends manipulate lightweight handles; heavy 3D buffers remain in the native Runtime.
- Initial control plane is JSON-RPC 2.0 over local IPC.
- Windows transport: Named Pipe.
- Linux/macOS transport: Unix Domain Socket.
- Shared memory / binary transport is reserved for future large-data transfer.
- Native format-specific data is preserved instead of being forced into the common abstraction.
- Autodesk FBX SDK support is optional and external to the public repository.
- Viewer / renderer support is optional and outside the processing path.

## Architecture

```text
Frontends
   ↓
Client SDKs
   ↓
JSON-RPC 2.0
   ↓
Local IPC
   ↓
unified3d-runtime
   ↓
Deterministic Operations
   ↓
Unified3D C++20 Core
   ↓
Format Adapters
   ↓
FBX / glTF / USD / ...
```

See the complete diagrams and component model in [docs/architecture/ARCHITECTURE.md](docs/architecture/ARCHITECTURE.md).

## Initial executable SDK slice

The first headless Python SDK operation validates the analysis-comparison contract observed through the private FBX and GLB analyzer prototypes:

```python
from unified3d import compare_analyses

result = compare_analyses(fbx_analysis, glb_analysis)
print(result.comparison)
```

It has no ComfyUI, frontend, renderer, asset-file or GPU dependency. The private ComfyUI comparator is now a thin adapter over this SDK API. The target production architecture remains Python/TypeScript clients calling the deterministic C++20 Runtime operation.

See [sdk/python/README.md](sdk/python/README.md).

The first shared wire contracts are now available as release candidates:

- [`unified3d.analysis/1.0-rc1`](schemas/unified3d.analysis-1.0-rc1.schema.json);
- [`unified3d.analysis-comparison/1.0-rc1`](schemas/unified3d.analysis-comparison-1.0-rc1.schema.json).

Their semantics and migration rules are documented in the
[Unified Analysis Schema 1.0 RC1 specification](docs/specifications/UNIFIED_ANALYSIS_SCHEMA_1.0_RC1.md).

## Initial native C++20 slice

The production-language implementation now contains three layers and a
persistent executable:

- `unified3d-core`: typed RC1 analysis records and semantic validation;
- `unified3d-operations`: native analysis-record comparison and compatibility
  levels 0–6;
- `unified3d-runtime-lib`: strict RC1 JSON codecs and JSON-RPC dispatch;
- `unified3d-runtime`: persistent newline-delimited stdio server used to test
  the protocol before the Windows Named Pipe transport is added.

Configure, build and test with one of the checked-in CMake presets:

```powershell
cmake --preset dev-gcc
cmake --build --preset dev-gcc
ctest --preset dev-gcc
```

GCC and Clang presets are provided. The native tests mirror the `thief` FBX/GLB
regression fixtures and assert parity with the Python reference operation at
both the C++ API and JSON-RPC boundaries. JSON parsing remains confined to the
Runtime: Core and Operations do not depend on a JSON library.

## Founding workflow

```text
Original High-Quality GLB
+
Rigged / Animated FBX
↓
Analyze
↓
Compare
↓
Transfer Skin
↓
Transfer Animation
↓
Validate
↓
High-Quality Rigged Animated GLB
```

The workflow should preserve the original GLB geometry, materials and textures while transferring the compatible skeleton, skin weights and animations from the rigged FBX.

## Repository documentation

- [Architecture](docs/architecture/ARCHITECTURE.md)
- [Implementation Specification v0.2.0](docs/specifications/Unified3D_Implementation_Specification_v0.2.0.md)
- [Unified Analysis Schema 1.0 RC1](docs/specifications/UNIFIED_ANALYSIS_SCHEMA_1.0_RC1.md)
- [RPC / IPC Protocol](docs/protocols/RPC_PROTOCOL.md)
- [Operation Model](docs/operations/OPERATIONS.md)
- [Format Adapter Model](docs/formats/FORMAT_ADAPTERS.md)
- [Documentation index](docs/README.md)
- [Contributing](CONTRIBUTING.md)
- [Security](SECURITY.md)

## Immediate roadmap

1. Stabilize the Autodesk FBX Analyzer reference.
2. Implement the GLB Analyzer.
3. Validate Unified Analysis Schema 1.0 RC1 in both analyzers.
4. Extend Geometry Compatibility Comparator from record levels 0–6 to spatial level 7.
5. Validate real FBX / GLB differences.
6. Introduce the minimum Core object model required by observed cases.
7. ~~Prototype `unified3d-runtime`.~~ Initial persistent stdio Runtime complete.
8. Prototype JSON-RPC 2.0 over Windows Named Pipe.
9. Create `@unified3d/client`.
10. Connect the first Workbench operation node.

## Repository state

The project is in early implementation. The Python SDK contains the reference
contract, legacy migration and current ComfyUI adapter API. The C++20 Core,
Operations and Runtime now provide native validation and comparison through a
persistent JSON-RPC process. Windows Named Pipe transport, native format
adapters and geometry access remain the next architectural milestones.

## License

The open-source license is intentionally **not selected yet**. The v0.2 specification lists Apache-2.0, MIT and BSD-3-Clause as candidates. A `LICENSE` file should be added only after that decision is made.
