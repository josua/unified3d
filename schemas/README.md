# Unified3D JSON schemas

This directory contains the wire-level contracts shared by analyzers, SDKs,
the Runtime and frontend adapters.

## Release candidates

- `unified3d.analysis/1.0-rc1` is the canonical analysis record.
- `unified3d.analysis-comparison/1.0-rc1` is the canonical comparison result.

Release-candidate identifiers are intentionally immutable. A breaking change
requires `rc2`; the final `1.0` identifier will only be published after both
the FBX and glTF adapters emit the same contract and the regression fixtures
pass through the Runtime implementation.

## Missing values

Unified3D uses one rule consistently:

- `0` means the analyzer measured the property and found none;
- `null` means the property was not computed or cannot be represented;
- omission is only allowed for format-native extension data.

JSON Schema files are the portable structural contract. The Python SDK also
performs semantic validation for invariants that JSON Schema cannot express
concisely, such as consistency between `present` flags and counts.
