# Unified3D Documentation

This directory is the canonical documentation entry point for the Unified3D repository.

## Canonical documents

| Area | Document | Status |
|---|---|---|
| Architecture | [architecture/ARCHITECTURE.md](architecture/ARCHITECTURE.md) | v0.2 |
| Implementation | [specifications/Unified3D_Implementation_Specification_v0.2.0.md](specifications/Unified3D_Implementation_Specification_v0.2.0.md) | Draft v0.2.0 |
| Analysis contract | [specifications/UNIFIED_ANALYSIS_SCHEMA_1.0_RC1.md](specifications/UNIFIED_ANALYSIS_SCHEMA_1.0_RC1.md) | 1.0 RC1 |
| RPC / IPC | [protocols/RPC_PROTOCOL.md](protocols/RPC_PROTOCOL.md) | Initial draft |
| Operations | [operations/OPERATIONS.md](operations/OPERATIONS.md) | Initial catalog |
| Format adapters | [formats/FORMAT_ADAPTERS.md](formats/FORMAT_ADAPTERS.md) | Initial contract summary |

## Documentation hierarchy

```text
docs/
├── architecture/
│   └── ARCHITECTURE.md
├── specifications/
│   ├── Unified3D_Implementation_Specification_v0.2.0.md
│   └── UNIFIED_ANALYSIS_SCHEMA_1.0_RC1.md
├── protocols/
│   └── RPC_PROTOCOL.md
├── formats/
│   └── FORMAT_ADAPTERS.md
└── operations/
    └── OPERATIONS.md
```

## Source-of-truth rule

The versioned implementation specification defines the detailed design intent. Architecture, protocol, format and operation documents are narrower projections of that specification intended to make the repository easier to navigate.

When a narrower document and the current versioned implementation specification disagree, the discrepancy should be resolved explicitly in a new specification revision rather than silently changing architectural behavior.
