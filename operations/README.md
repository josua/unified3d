# Unified3D Operations

`unified3d-operations` contains deterministic C++20 business operations over
Core values.

The first native operation is:

```cpp
compare_analysis_records(a, b)
```

It validates both records, detects the rig-donor/dense-target pattern and
evaluates compatibility evidence levels 0–6. Invalid records produce
diagnostics and no comparison value. Spatial level 7 remains a future operation
requiring decoded geometry buffers.
