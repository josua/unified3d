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
class CanonicalGeometryFingerprint:
    algorithm: str
    digest: str
    triangle_count: int
    position_tolerance_m: float

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> "CanonicalGeometryFingerprint":
        return cls(
            algorithm=str(value["algorithm"]),
            digest=str(value["digest"]),
            triangle_count=int(value["triangle_count"]),
            position_tolerance_m=float(value["position_tolerance_m"]),
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
    canonical_geometry_fingerprint: CanonicalGeometryFingerprint | None = None

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
            canonical_geometry_fingerprint=CanonicalGeometryFingerprint.from_dict(
                value["canonical_geometry_fingerprint"]
            )
            if isinstance(value.get("canonical_geometry_fingerprint"), dict)
            else None,
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
class GlbToFbxConversionReport:
    source_path: Path
    output_path: Path
    source_size_bytes: int
    output_size_bytes: int
    mesh_count: int
    primitive_count: int
    control_point_count: int
    triangle_count: int
    material_count: int
    texture_count: int
    embedded_media_count: int
    geometry_preserved: bool
    media_embedded: bool

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> "GlbToFbxConversionReport":
        return cls(
            source_path=Path(str(value["source_path"])),
            output_path=Path(str(value["output_path"])),
            source_size_bytes=int(value["source_size_bytes"]),
            output_size_bytes=int(value["output_size_bytes"]),
            mesh_count=int(value["mesh_count"]),
            primitive_count=int(value["primitive_count"]),
            control_point_count=int(value["control_point_count"]),
            triangle_count=int(value["triangle_count"]),
            material_count=int(value["material_count"]),
            texture_count=int(value["texture_count"]),
            embedded_media_count=int(value["embedded_media_count"]),
            geometry_preserved=bool(value["geometry_preserved"]),
            media_embedded=bool(value["media_embedded"]),
        )


@dataclass(frozen=True, slots=True)
class ConvertGlbToFbxResult:
    schema: str
    source_asset: AssetHandle
    converted_asset: AssetHandle
    report: GlbToFbxConversionReport

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> "ConvertGlbToFbxResult":
        return cls(
            schema=str(value["schema"]),
            source_asset=AssetHandle.from_dict(value["source_asset"]),
            converted_asset=AssetHandle.from_dict(value["converted_asset"]),
            report=GlbToFbxConversionReport.from_dict(value["report"]),
        )


@dataclass(frozen=True, slots=True)
class SpatialNormalizationReport:
    source_path: Path
    output_path: Path
    source_size_bytes: int
    output_size_bytes: int
    root_node_index: int
    root_node_name: str
    absorbed_uniform_scale: float
    position_height_m: float
    modified_node_translation_count: int
    modified_animation_accessor_count: int
    modified_inverse_bind_matrix_count: int
    removed_emissive_texture_count: int
    zeroed_emissive_factor_count: int
    removed_head_helper_node_count: int
    removed_head_helper_joint_count: int
    removed_head_helper_animation_channel_count: int
    removed_animation_clip_count: int
    removed_animation_channel_count: int
    removed_animation_sampler_count: int
    scale_correction_applied: bool
    emissive_correction_applied: bool
    head_helper_bone_removal_applied: bool
    animation_removal_applied: bool

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> "SpatialNormalizationReport":
        return cls(
            source_path=Path(str(value["source_path"])),
            output_path=Path(str(value["output_path"])),
            source_size_bytes=int(value["source_size_bytes"]),
            output_size_bytes=int(value["output_size_bytes"]),
            root_node_index=int(value["root_node_index"]),
            root_node_name=str(value["root_node_name"]),
            absorbed_uniform_scale=float(value["absorbed_uniform_scale"]),
            position_height_m=float(value["position_height_m"]),
            modified_node_translation_count=int(value["modified_node_translation_count"]),
            modified_animation_accessor_count=int(
                value["modified_animation_accessor_count"]
            ),
            modified_inverse_bind_matrix_count=int(
                value["modified_inverse_bind_matrix_count"]
            ),
            removed_emissive_texture_count=int(
                value.get("removed_emissive_texture_count", 0)
            ),
            zeroed_emissive_factor_count=int(
                value.get("zeroed_emissive_factor_count", 0)
            ),
            removed_head_helper_node_count=int(
                value.get("removed_head_helper_node_count", 0)
            ),
            removed_head_helper_joint_count=int(
                value.get("removed_head_helper_joint_count", 0)
            ),
            removed_head_helper_animation_channel_count=int(
                value.get("removed_head_helper_animation_channel_count", 0)
            ),
            removed_animation_clip_count=int(
                value.get("removed_animation_clip_count", 0)
            ),
            removed_animation_channel_count=int(
                value.get("removed_animation_channel_count", 0)
            ),
            removed_animation_sampler_count=int(
                value.get("removed_animation_sampler_count", 0)
            ),
            scale_correction_applied=bool(
                value.get("scale_correction_applied", True)
            ),
            emissive_correction_applied=bool(
                value.get("emissive_correction_applied", True)
            ),
            head_helper_bone_removal_applied=bool(
                value.get("head_helper_bone_removal_applied", False)
            ),
            animation_removal_applied=bool(
                value.get("animation_removal_applied", False)
            ),
        )


@dataclass(frozen=True, slots=True)
class NormalizeSpatialResult:
    schema: str
    source_asset: AssetHandle
    normalized_asset: AssetHandle
    report: SpatialNormalizationReport

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> "NormalizeSpatialResult":
        return cls(
            schema=str(value["schema"]),
            source_asset=AssetHandle.from_dict(value["source_asset"]),
            normalized_asset=AssetHandle.from_dict(value["normalized_asset"]),
            report=SpatialNormalizationReport.from_dict(value["report"]),
        )


@dataclass(frozen=True, slots=True)
class SpatialMappingSample:
    target_vertex: int
    source_triangle: int
    barycentric: tuple[float, float, float]
    distance_m: float

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> "SpatialMappingSample":
        barycentric = tuple(float(item) for item in value["barycentric"])
        if len(barycentric) != 3:
            raise ValueError("Runtime returned invalid barycentric coordinates")
        return cls(
            target_vertex=int(value["target_vertex"]),
            source_triangle=int(value["source_triangle"]),
            barycentric=(barycentric[0], barycentric[1], barycentric[2]),
            distance_m=float(value["distance_m"]),
        )


@dataclass(frozen=True, slots=True)
class SkinTransferReport:
    source_triangle_count: int
    target_vertex_count: int
    matched_vertex_count: int
    rejected_vertex_count: int
    mean_distance_m: float
    maximum_distance_m: float
    output_max_influences: int
    diagnostic_samples: tuple[SpatialMappingSample, ...]

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> "SkinTransferReport":
        return cls(
            source_triangle_count=int(value["source_triangle_count"]),
            target_vertex_count=int(value["target_vertex_count"]),
            matched_vertex_count=int(value["matched_vertex_count"]),
            rejected_vertex_count=int(value["rejected_vertex_count"]),
            mean_distance_m=float(value["mean_distance_m"]),
            maximum_distance_m=float(value["maximum_distance_m"]),
            output_max_influences=int(value["output_max_influences"]),
            diagnostic_samples=tuple(
                SpatialMappingSample.from_dict(item)
                for item in value.get("diagnostic_samples", [])
            ),
        )


@dataclass(frozen=True, slots=True)
class SkinTransferResult:
    schema: str
    method: str
    source_asset: AssetHandle
    target_asset: AssetHandle
    report: SkinTransferReport

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> "SkinTransferResult":
        return cls(
            schema=str(value["schema"]),
            method=str(value["method"]),
            source_asset=AssetHandle.from_dict(value["source_asset"]),
            target_asset=AssetHandle.from_dict(value["target_asset"]),
            report=SkinTransferReport.from_dict(value["report"]),
        )


@dataclass(frozen=True, slots=True)
class RuntimeComparisonResult:
    payload: dict[str, Any]

    @property
    def comparison(self) -> dict[str, Any]:
        return self.payload["comparison"]

    def to_dict(self) -> dict[str, Any]:
        from copy import deepcopy

        return deepcopy(self.payload)
