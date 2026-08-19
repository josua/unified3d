"""Typed, UI-independent models for the Unified3D analysis contract."""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass
from typing import Any, Literal, TypedDict, cast


AnalysisFormat = Literal["fbx", "gltf", "unknown"]
AnalysisContainer = Literal["fbx", "glb", "gltf", "unknown"]
Presence = bool | None


class AnalyzerSection(TypedDict):
    name: str
    backend: str
    backend_version: str | None
    analyzer_version: str | None


class CoordinateSystemSection(TypedDict):
    handedness: Literal["left", "right"] | None
    up_axis: str | None
    forward_axis: str | None
    unit: str | None
    meters_per_unit: float | None


class AssetSection(TypedDict):
    path: str | None
    format: AnalysisFormat
    container: AnalysisContainer
    version: str | None
    generator: str | None
    size_bytes: int | None
    coordinate_system: CoordinateSystemSection


class BoundsSection(TypedDict):
    min: list[float]
    max: list[float]


class SceneSection(TypedDict):
    scene_count: int | None
    node_count: int | None
    mesh_instance_count: int | None
    bounds: BoundsSection | None


class TopologySignatureSection(TypedDict):
    algorithm: str
    digest: str
    domain: str


class GeometrySection(TypedDict):
    mesh_count: int | None
    primitive_count: int | None
    geometric_vertex_count: int | None
    geometric_vertex_semantic: Literal["control_points", "unique_positions"] | None
    render_vertex_count: int | None
    index_count: int | None
    polygon_vertex_count: int | None
    polygon_count: int | None
    triangle_count: int | None
    degenerate_triangle_count: int | None
    ngon_count: int | None
    uv_channel_count: int | None
    uv_set_binding_count: int | None
    normal_count: int | None
    tangent_count: int | None
    color_attribute_count: int | None
    bounds: BoundsSection | None
    topology_signature: TopologySignatureSection | None


class MaterialsSection(TypedDict):
    material_resource_count: int | None
    material_binding_count: int | None
    texture_resource_count: int | None


class SkeletonSection(TypedDict):
    present: Presence
    joint_count: int | None
    root_joint_count: int | None
    hierarchy_signature: str | None
    bind_pose_signature: str | None
    joint_names: list[str] | None


class SkinSection(TypedDict):
    present: Presence
    skin_count: int | None
    cluster_count: int | None
    skinned_vertex_count: int | None
    max_influences: int | None
    influence_set_count: int | None


class AnimationSection(TypedDict):
    present: Presence
    clip_count: int | None
    channel_count: int | None
    sampler_count: int | None
    duration_seconds: float | None


class AnalysisDiagnostic(TypedDict):
    severity: Literal["info", "warning", "error"]
    code: str
    message: str
    path: str | None


class AnalysisRecordDict(TypedDict):
    status: Literal["ok"]
    schema: Literal["unified3d.analysis/1.0-rc1"]
    schema_status: Literal["release-candidate"]
    analyzer: AnalyzerSection
    asset: AssetSection
    scene: SceneSection
    geometry: GeometrySection
    materials: MaterialsSection
    skeleton: SkeletonSection
    skin: SkinSection
    animation: AnimationSection
    native: dict[str, Any]
    diagnostics: list[AnalysisDiagnostic]


@dataclass(frozen=True, slots=True)
class AnalysisRecord:
    """Top-level immutable canonical analysis record.

    Nested dictionaries are deliberately retained at the wire boundary. This
    keeps serialization allocation-light while the top-level dataclass gives
    SDK callers a stable, typed object and isolated source/export operations.
    Callers should treat nested sections as read-only.
    """

    analyzer: AnalyzerSection
    asset: AssetSection
    scene: SceneSection
    geometry: GeometrySection
    materials: MaterialsSection
    skeleton: SkeletonSection
    skin: SkinSection
    animation: AnimationSection
    native: dict[str, Any]
    diagnostics: tuple[AnalysisDiagnostic, ...]

    @classmethod
    def from_dict(cls, value: AnalysisRecordDict) -> "AnalysisRecord":
        data = deepcopy(value)
        return cls(
            analyzer=cast(AnalyzerSection, data["analyzer"]),
            asset=cast(AssetSection, data["asset"]),
            scene=cast(SceneSection, data["scene"]),
            geometry=cast(GeometrySection, data["geometry"]),
            materials=cast(MaterialsSection, data["materials"]),
            skeleton=cast(SkeletonSection, data["skeleton"]),
            skin=cast(SkinSection, data["skin"]),
            animation=cast(AnimationSection, data["animation"]),
            native=cast(dict[str, Any], data["native"]),
            diagnostics=tuple(cast(list[AnalysisDiagnostic], data["diagnostics"])),
        )

    def to_dict(self) -> AnalysisRecordDict:
        return cast(
            AnalysisRecordDict,
            deepcopy(
                {
                    "status": "ok",
                    "schema": "unified3d.analysis/1.0-rc1",
                    "schema_status": "release-candidate",
                    "analyzer": self.analyzer,
                    "asset": self.asset,
                    "scene": self.scene,
                    "geometry": self.geometry,
                    "materials": self.materials,
                    "skeleton": self.skeleton,
                    "skin": self.skin,
                    "animation": self.animation,
                    "native": self.native,
                    "diagnostics": list(self.diagnostics),
                }
            ),
        )
