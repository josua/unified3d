# unified3d
Headless 3D Asset Processing Framework
> Analyze, compare, transform, rig, animate, validate and convert 3D assets without requiring a DCC, viewport or renderer.

**Status:** Architecture / specification phase  
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
- [RPC / IPC Protocol](docs/protocols/RPC_PROTOCOL.md)
- [Operation Model](docs/operations/OPERATIONS.md)
- [Format Adapter Model](docs/formats/FORMAT_ADAPTERS.md)
- [Documentation index](docs/README.md)
- [Contributing](CONTRIBUTING.md)
- [Security](SECURITY.md)

## Immediate roadmap

1. Stabilize the Autodesk FBX Analyzer reference.
2. Implement the GLB Analyzer.
3. Stabilize Unified Analysis Schema 1.0.
4. Implement Geometry Compatibility Comparator.
5. Validate real FBX / GLB differences.
6. Introduce the minimum Core object model required by observed cases.
7. Prototype `unified3d-runtime`.
8. Prototype JSON-RPC 2.0 over Named Pipe.
9. Create `@unified3d/client`.
10. Connect the first Workbench operation node.

## Repository state

The project is currently documentation-first. The source tree described in the architecture specification will be introduced progressively as implementation begins, rather than creating empty code directories with no executable contract behind them.

## License

The open-source license is intentionally **not selected yet**. The v0.2 specification lists Apache-2.0, MIT and BSD-3-Clause as candidates. A `LICENSE` file should be added only after that decision is made.
