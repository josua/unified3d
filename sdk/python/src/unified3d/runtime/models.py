"""Typed lightweight Runtime handles and RPC result models."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Literal


@dataclass(frozen=True, slots=True)
class Provenance:
    producer: str
    operation_id: str
    source_uri: str | None
    source_revision: str | None
    parents: tuple[str, ...]

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> "Provenance":
        return cls(
            producer=str(value["producer"]),
            operation_id=str(value["operation_id"]),
            source_uri=value.get("source_uri"),
            source_revision=value.get("source_revision"),
            parents=tuple(str(item) for item in value.get("parents", [])),
        )


@dataclass(frozen=True, slots=True)
class VertexBufferHandle:
    id: str
    kind: Literal["VERTEX_BUFFER"]
    session: str
    generation: int
    object_id: int
    semantic: str
    scalar_type: str
    component_count: int
    element_count: int
    byte_length: int
    provenance: Provenance

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> "VertexBufferHandle":
        if value.get("kind") != "VERTEX_BUFFER":
            raise ValueError("Runtime returned a non-vertex-buffer handle")
        return cls(
            id=str(value["id"]), kind="VERTEX_BUFFER",
            session=str(value["session"]), generation=int(value["generation"]),
            object_id=int(value["object_id"]), semantic=str(value["semantic"]),
            scalar_type=str(value["scalar_type"]),
            component_count=int(value["component_count"]),
            element_count=int(value["element_count"]),
            byte_length=int(value["byte_length"]),
            provenance=Provenance.from_dict(value["provenance"]),
        )


@dataclass(frozen=True, slots=True)
class IndexBufferHandle:
    id: str
    kind: Literal["INDEX_BUFFER"]
    session: str
    generation: int
    object_id: int
    scalar_type: str
    element_count: int
    byte_length: int
    provenance: Provenance

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> "IndexBufferHandle":
        if value.get("kind") != "INDEX_BUFFER":
            raise ValueError("Runtime returned a non-index-buffer handle")
        return cls(
            id=str(value["id"]), kind="INDEX_BUFFER",
            session=str(value["session"]), generation=int(value["generation"]),
            object_id=int(value["object_id"]), scalar_type=str(value["scalar_type"]),
            element_count=int(value["element_count"]),
            byte_length=int(value["byte_length"]),
            provenance=Provenance.from_dict(value["provenance"]),
        )


@dataclass(frozen=True, slots=True)
class SkinWeightBufferHandle:
    id: str
    kind: Literal["SKIN_WEIGHT_BUFFER"]
    session: str
    generation: int
    object_id: int
    influence_set: int
    vertex_count: int
    component_count: int
    joint_scalar_type: str
    weight_scalar_type: str
    byte_length: int
    provenance: Provenance

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> "SkinWeightBufferHandle":
        if value.get("kind") != "SKIN_WEIGHT_BUFFER":
            raise ValueError("Runtime returned a non-skin-weight-buffer handle")
        return cls(
            id=str(value["id"]), kind="SKIN_WEIGHT_BUFFER",
            session=str(value["session"]), generation=int(value["generation"]),
            object_id=int(value["object_id"]), influence_set=int(value["influence_set"]),
            vertex_count=int(value["vertex_count"]),
            component_count=int(value["component_count"]),
            joint_scalar_type=str(value["joint_scalar_type"]),
            weight_scalar_type=str(value["weight_scalar_type"]),
            byte_length=int(value["byte_length"]),
            provenance=Provenance.from_dict(value["provenance"]),
        )


@dataclass(frozen=True, slots=True)
class PrimitiveResources:
    name: str
    source_mesh_index: int
    source_primitive_index: int
    domain: str
    local_to_world: tuple[float, ...]
    max_influences: int
    positions: VertexBufferHandle
    indices: IndexBufferHandle | None
    influence_sets: tuple[SkinWeightBufferHandle, ...]

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> "PrimitiveResources":
        transform = tuple(float(item) for item in value["local_to_world"])
        if len(transform) != 16:
            raise ValueError("Runtime returned a non-4x4 primitive transform")
        index_value = value.get("indices")
        return cls(
            name=str(value["name"]),
            source_mesh_index=int(value["source_mesh_index"]),
            source_primitive_index=int(value["source_primitive_index"]),
            domain=str(value["domain"]), local_to_world=transform,
            max_influences=int(value["max_influences"]),
            positions=VertexBufferHandle.from_dict(value["positions"]),
            indices=IndexBufferHandle.from_dict(index_value)
            if isinstance(index_value, dict) else None,
            influence_sets=tuple(
                SkinWeightBufferHandle.from_dict(item)
                for item in value.get("influence_sets", [])
            ),
        )


@dataclass(frozen=True, slots=True)
class AssetHandle:
    id: str
    kind: Literal["3D_ASSET"]
    session: str
    generation: int
    object_id: int
    format: str
    container: str
    path: Path
    size_bytes: int
    retain_count: int
    provenance: Provenance
    adapter: str | None = None
    buffer_coordinate_system: str | None = None
    buffer_unit_meters: float | None = None
    joint_names: tuple[str, ...] = ()
    primitives: tuple[PrimitiveResources, ...] = ()

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> "AssetHandle":
        if value.get("kind") != "3D_ASSET":
            raise ValueError("Runtime returned a non-asset handle")
        return cls(
            id=str(value["id"]),
            kind="3D_ASSET",
            session=str(value["session"]),
            generation=int(value["generation"]),
            object_id=int(value["object_id"]),
            format=str(value["format"]),
            container=str(value["container"]),
            path=Path(str(value["path"])),
            size_bytes=int(value["size_bytes"]),
            retain_count=int(value["retain_count"]),
            provenance=Provenance.from_dict(value["provenance"]),
            adapter=str(value["adapter"]) if value.get("adapter") is not None else None,
            buffer_coordinate_system=str(value["buffer_coordinate_system"])
            if value.get("buffer_coordinate_system") is not None else None,
            buffer_unit_meters=float(value["buffer_unit_meters"])
            if value.get("buffer_unit_meters") is not None else None,
            joint_names=tuple(str(item) for item in value.get("joint_names", [])),
            primitives=tuple(
                PrimitiveResources.from_dict(item)
                for item in value.get("primitives", [])
            ),
        )

    def to_wire(self) -> dict[str, Any]:
        return {
            "id": self.id,
            "kind": self.kind,
            "session": self.session,
            "generation": self.generation,
            "object_id": self.object_id,
        }


@dataclass(frozen=True, slots=True)
class LoadAssetResult:
    asset: AssetHandle
    reused: bool


@dataclass(frozen=True, slots=True)
class ReleaseAssetResult:
    released: bool
    remaining_references: int


@dataclass(frozen=True, slots=True)
class RuntimeComparisonResult:
    payload: dict[str, Any]

    @property
    def comparison(self) -> dict[str, Any]:
        return self.payload["comparison"]

    def to_dict(self) -> dict[str, Any]:
        from copy import deepcopy

        return deepcopy(self.payload)
