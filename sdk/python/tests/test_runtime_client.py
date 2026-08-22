import json
import os
import subprocess
import sys
import tempfile
import unittest
import uuid
from pathlib import Path
from typing import Any, Callable


PYTHON_ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = PYTHON_ROOT.parents[1]
sys.path.insert(0, str(PYTHON_ROOT / "src"))

from unified3d import (  # noqa: E402
    AssetHandle,
    RuntimeRPCError,
    Unified3DClient,
    compare_analyses,
    compare_analyses_oracle,
)
from unified3d.runtime import NamedPipeTransport, RuntimeTransport  # noqa: E402


ResponseFactory = Callable[[dict[str, Any]], dict[str, Any]]


class FakeTransport(RuntimeTransport):
    def __init__(self, response_factory: ResponseFactory) -> None:
        self.response_factory = response_factory
        self.requests: list[dict[str, Any]] = []
        self.closed = False

    def exchange(self, request: bytes) -> bytes:
        payload = json.loads(request.decode("utf-8"))
        self.requests.append(payload)
        return json.dumps(self.response_factory(payload)).encode("utf-8")

    def close(self) -> None:
        self.closed = True


def successful_response(
    result: dict[str, Any],
) -> ResponseFactory:
    def respond(request: dict[str, Any]) -> dict[str, Any]:
        return {"jsonrpc": "2.0", "id": request["id"], "result": result}

    return respond


class RuntimeClientUnitTests(unittest.TestCase):
    def test_hello_uses_typed_json_rpc_request(self) -> None:
        transport = FakeTransport(successful_response({"runtime_version": "test"}))
        client = Unified3DClient(transport)

        self.assertEqual(client.hello()["runtime_version"], "test")
        self.assertEqual(
            transport.requests,
            [{"jsonrpc": "2.0", "id": 1, "method": "runtime.hello"}],
        )

    def test_structured_runtime_error_is_preserved(self) -> None:
        def respond(request: dict[str, Any]) -> dict[str, Any]:
            return {
                "jsonrpc": "2.0",
                "id": request["id"],
                "error": {
                    "code": -32020,
                    "message": "Invalid resource handle",
                    "data": {"code": "STALE_HANDLE"},
                },
            }

        client = Unified3DClient(FakeTransport(respond))
        with self.assertRaises(RuntimeRPCError) as raised:
            client.request("asset.release", {})

        self.assertEqual(raised.exception.code, -32020)
        self.assertEqual(raised.exception.data, {"code": "STALE_HANDLE"})

    def test_load_result_is_converted_to_a_typed_handle(self) -> None:
        asset = {
            "id": "asset:session:4:12",
            "kind": "3D_ASSET",
            "session": "session",
            "generation": 4,
            "object_id": 12,
            "format": "GLTF",
            "container": "GLB",
            "path": "C:/assets/character.glb",
            "size_bytes": 42,
            "retain_count": 1,
            "canonical_geometry_fingerprint": {
                "algorithm": "triangle-position-soup-fnv1a64-v1",
                "digest": "1757923a1ed0f716",
                "triangle_count": 298075,
                "position_tolerance_m": 0.00001,
            },
            "provenance": {
                "producer": "asset.load",
                "operation_id": "load:session:4:12",
                "source_uri": "C:/assets/character.glb",
                "source_revision": "revision",
                "parents": [],
            },
        }
        transport = FakeTransport(
            successful_response({"asset": asset, "reused": False})
        )
        client = Unified3DClient(transport)

        loaded = client.load_asset("C:/assets/character.glb")

        self.assertEqual(loaded.asset.id, "asset:session:4:12")
        self.assertEqual(loaded.asset.generation, 4)
        self.assertEqual(loaded.asset.provenance.producer, "asset.load")
        self.assertEqual(
            loaded.asset.canonical_geometry_fingerprint.digest,
            "1757923a1ed0f716",
        )
        self.assertFalse(loaded.reused)

    def test_spatial_normalization_returns_typed_assets_and_report(self) -> None:
        def asset(identifier: str, path: str, producer: str) -> dict[str, Any]:
            return {
                "id": identifier,
                "kind": "3D_ASSET",
                "session": "session",
                "generation": 1,
                "object_id": int(identifier.rsplit(":", 1)[1]),
                "format": "GLTF",
                "container": "GLB",
                "path": path,
                "size_bytes": 42,
                "retain_count": 1,
                "provenance": {
                    "producer": producer,
                    "operation_id": f"{producer}:session",
                    "source_uri": path,
                    "source_revision": None,
                    "parents": [],
                },
            }

        source = asset("asset:session:1:1", "C:/assets/source.glb", "asset.load")
        normalized = asset(
            "asset:session:1:2",
            "C:/assets/source_unified3d_normalized.glb",
            "asset.normalize_spatial",
        )
        transport = FakeTransport(
            successful_response(
                {
                    "schema": "unified3d.spatial-normalization/1.0-draft",
                    "source_asset": source,
                    "normalized_asset": normalized,
                    "report": {
                        "source_path": source["path"],
                        "output_path": normalized["path"],
                        "source_size_bytes": 42,
                        "output_size_bytes": 84,
                        "root_node_index": 25,
                        "root_node_name": "Armature",
                        "absorbed_uniform_scale": 0.01,
                        "position_height_m": 1.7,
                        "modified_node_translation_count": 24,
                        "modified_animation_accessor_count": 48,
                        "modified_inverse_bind_matrix_count": 24,
                        "removed_emissive_texture_count": 1,
                        "zeroed_emissive_factor_count": 1,
                        "removed_head_helper_node_count": 2,
                        "removed_head_helper_joint_count": 2,
                        "removed_head_helper_animation_channel_count": 6,
                        "removed_animation_clip_count": 1,
                        "removed_animation_channel_count": 66,
                        "removed_animation_sampler_count": 66,
                        "scale_correction_applied": True,
                        "emissive_correction_applied": True,
                        "head_helper_bone_removal_applied": True,
                        "animation_removal_applied": True,
                    },
                }
            )
        )
        client = Unified3DClient(transport)
        loaded_source = AssetHandle.from_dict(source)
        result = client.normalize_spatial(
            loaded_source,
            "C:/assets/source_unified3d_normalized.glb",
            remove_head_helper_bones=True,
            remove_animations=True,
        )

        self.assertEqual(result.report.root_node_name, "Armature")
        self.assertAlmostEqual(result.report.absorbed_uniform_scale, 0.01)
        self.assertEqual(result.report.removed_emissive_texture_count, 1)
        self.assertEqual(result.report.zeroed_emissive_factor_count, 1)
        self.assertEqual(result.report.removed_head_helper_node_count, 2)
        self.assertEqual(result.report.removed_head_helper_joint_count, 2)
        self.assertEqual(result.report.removed_head_helper_animation_channel_count, 6)
        self.assertEqual(result.report.removed_animation_clip_count, 1)
        self.assertEqual(result.report.removed_animation_channel_count, 66)
        self.assertEqual(result.report.removed_animation_sampler_count, 66)
        self.assertTrue(result.report.scale_correction_applied)
        self.assertTrue(result.report.emissive_correction_applied)
        self.assertTrue(result.report.head_helper_bone_removal_applied)
        self.assertTrue(result.report.animation_removal_applied)
        self.assertTrue(transport.requests[0]["params"]["correct_scale_factor"])
        self.assertTrue(transport.requests[0]["params"]["remove_emissive_channel"])
        self.assertTrue(transport.requests[0]["params"]["remove_head_helper_bones"])
        self.assertTrue(transport.requests[0]["params"]["remove_animations"])
        self.assertEqual(
            result.normalized_asset.provenance.producer,
            "asset.normalize_spatial",
        )

    def test_glb_to_fbx_conversion_is_typed_and_requests_embedded_media(self) -> None:
        def asset(
            identifier: str,
            path: str,
            container: str,
            producer: str,
        ) -> dict[str, Any]:
            return {
                "id": identifier,
                "kind": "3D_ASSET",
                "session": "session",
                "generation": 1,
                "object_id": int(identifier.rsplit(":", 1)[1]),
                "format": "GLTF" if container == "GLB" else "FBX",
                "container": container,
                "path": path,
                "size_bytes": 42,
                "retain_count": 1,
                "provenance": {
                    "producer": producer,
                    "operation_id": f"{producer}:session",
                    "source_uri": path,
                    "source_revision": None,
                    "parents": [],
                },
            }

        source = asset(
            "asset:session:1:1", "C:/assets/goblin.glb", "GLB", "asset.load"
        )
        converted = asset(
            "asset:session:1:2",
            "C:/assets/goblin.fbx",
            "FBX",
            "asset.convert_glb_to_fbx",
        )
        transport = FakeTransport(
            successful_response(
                {
                    "schema": "unified3d.glb-to-fbx-conversion/1.0-draft",
                    "source_asset": source,
                    "converted_asset": converted,
                    "report": {
                        "source_path": source["path"],
                        "output_path": converted["path"],
                        "source_size_bytes": 117806172,
                        "output_size_bytes": 142000000,
                        "mesh_count": 16,
                        "primitive_count": 16,
                        "control_point_count": 1047168,
                        "triangle_count": 1925873,
                        "material_count": 16,
                        "texture_count": 48,
                        "embedded_media_count": 48,
                        "geometry_preserved": True,
                        "media_embedded": True,
                    },
                }
            )
        )
        client = Unified3DClient(transport)

        result = client.convert_glb_to_fbx(
            AssetHandle.from_dict(source),
            "C:/assets/goblin.fbx",
            embed_media=True,
            overwrite=True,
        )

        self.assertEqual(result.report.triangle_count, 1925873)
        self.assertEqual(result.report.embedded_media_count, 48)
        self.assertTrue(result.report.geometry_preserved)
        self.assertTrue(result.report.media_embedded)
        self.assertEqual(result.converted_asset.container, "FBX")
        self.assertEqual(
            result.converted_asset.provenance.producer,
            "asset.convert_glb_to_fbx",
        )
        self.assertEqual(
            transport.requests[0]["method"], "asset.convert_glb_to_fbx"
        )
        self.assertTrue(transport.requests[0]["params"]["embed_media"])
        self.assertTrue(transport.requests[0]["params"]["overwrite"])

    def test_spatial_normalization_rejects_all_corrections_disabled(self) -> None:
        asset = AssetHandle.from_dict(
            {
                "id": "asset:session:1:1",
                "kind": "3D_ASSET",
                "session": "session",
                "generation": 1,
                "object_id": 1,
                "format": "GLTF",
                "container": "GLB",
                "path": "C:/assets/source.glb",
                "size_bytes": 42,
                "retain_count": 1,
                "provenance": {
                    "producer": "asset.load",
                    "operation_id": "load:session",
                    "source_uri": "C:/assets/source.glb",
                    "source_revision": None,
                    "parents": [],
                },
            }
        )
        transport = FakeTransport(successful_response({}))
        client = Unified3DClient(transport)

        with self.assertRaisesRegex(ValueError, "at least one correction"):
            client.normalize_spatial(
                asset,
                "C:/assets/output.glb",
                correct_scale_factor=False,
                remove_emissive_channel=False,
            )
        self.assertEqual(transport.requests, [])

    def test_skin_transfer_is_typed_and_keeps_large_buffers_in_runtime(self) -> None:
        def asset(identifier: str, path: str, joint_names: list[str]) -> dict[str, Any]:
            return {
                "id": identifier,
                "kind": "3D_ASSET",
                "session": "session",
                "generation": 1,
                "object_id": int(identifier.rsplit(":", 1)[1]),
                "format": "FBX" if path.endswith(".fbx") else "GLTF",
                "container": "FBX" if path.endswith(".fbx") else "GLB",
                "path": path,
                "size_bytes": 42,
                "retain_count": 1,
                "joint_names": joint_names,
                "provenance": {
                    "producer": "asset.load",
                    "operation_id": f"load:{identifier}",
                    "source_uri": path,
                    "source_revision": None,
                    "parents": [],
                },
            }

        source = asset("asset:session:1:1", "C:/assets/rig.fbx", ["Hips", "Spine"])
        target = asset("asset:session:1:2", "C:/assets/high.glb", ["Hips", "Spine"])
        transport = FakeTransport(successful_response({
            "schema": "unified3d.skin-transfer/1.0-draft",
            "method": "spatial_surface",
            "source_asset": source,
            "target_asset": target,
            "report": {
                "source_triangle_count": 50059,
                "target_vertex_count": 1045852,
                "matched_vertex_count": 1045852,
                "rejected_vertex_count": 0,
                "mean_distance_m": 0.0004,
                "maximum_distance_m": 0.003,
                "output_max_influences": 4,
                "diagnostic_samples": [],
            },
        }))
        client = Unified3DClient(transport)
        result = client.transfer_skin(
            AssetHandle.from_dict(source),
            AssetHandle.from_dict(target),
            maximum_distance_m=0.01,
            replace_existing=True,
        )

        self.assertEqual(result.method, "spatial_surface")
        self.assertEqual(result.report.target_vertex_count, 1045852)
        self.assertEqual(result.report.output_max_influences, 4)
        self.assertNotIn("weights", transport.requests[0]["params"])
        self.assertEqual(transport.requests[0]["method"], "skin.transfer")
        self.assertEqual(transport.requests[0]["params"]["maximum_distance_m"], 0.01)
        self.assertIs(transport.requests[0]["params"]["replace_existing"], True)

    def test_local_comparator_remains_the_named_oracle(self) -> None:
        self.assertIs(compare_analyses_oracle, compare_analyses)


def runtime_executable() -> Path | None:
    configured = os.environ.get("UNIFIED3D_RUNTIME_EXECUTABLE")
    candidates = [Path(configured)] if configured else [
        REPOSITORY_ROOT / "build" / "msvc-autodesk" / "runtime" / "Release" / "unified3d-runtime.exe",
        REPOSITORY_ROOT / "build" / "dev-gcc" / "runtime" / "unified3d-runtime.exe",
    ]
    return next((candidate for candidate in candidates if candidate.is_file()), None)


@unittest.skipUnless(
    runtime_executable() is not None,
    "Set UNIFIED3D_RUNTIME_EXECUTABLE to run native Runtime integration tests",
)
class NativeRuntimeIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.executable = runtime_executable()
        assert cls.executable is not None
        fixture_root = PYTHON_ROOT / "tests" / "fixtures"
        cls.fbx = json.loads(
            (fixture_root / "thief-fbx.analysis-1.0-rc1.json").read_text(
                encoding="utf-8"
            )
        )
        cls.glb = json.loads(
            (fixture_root / "thief-glb.analysis-1.0-rc1.json").read_text(
                encoding="utf-8"
            )
        )

    def test_stdio_client_matches_oracle_for_contract_outcomes(self) -> None:
        client = Unified3DClient.connect_stdio(self.executable)
        try:
            hello = client.hello()
            self.assertIn("asset.load", hello["capabilities"])
            self.assertIn("asset.normalize_spatial", hello["capabilities"])
            self.assertIn("skin.transfer", hello["capabilities"])
            if hello["native_adapters"].get("autodesk_fbx"):
                self.assertIn(
                    "asset.convert_glb_to_fbx", hello["capabilities"]
                )
            else:
                self.assertNotIn(
                    "asset.convert_glb_to_fbx", hello["capabilities"]
                )

            remote = client.compare_analyses(self.fbx, self.glb).comparison
            oracle = compare_analyses_oracle(self.fbx, self.glb).comparison
            self.assertEqual(
                remote["compatibility"]["classification"],
                oracle["compatibility"]["classification"],
            )
            self.assertEqual(
                remote["compatibility"]["recommended_next_level"],
                oracle["compatibility"]["recommended_next_level"],
            )
            self.assertEqual(
                [
                    level["status"]
                    for level in remote["compatibility"]["levels"]
                ],
                [
                    level["status"]
                    for level in oracle["compatibility"]["levels"]
                ],
            )

            with tempfile.NamedTemporaryFile(suffix=".gltf", delete=False) as stream:
                stream.write(
                    b'{"asset":{"version":"2.0"},'
                    b'"buffers":[{"uri":"data:application/octet-stream;base64,'
                    b'AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA",'
                    b'"byteLength":42}],'
                    b'"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},'
                    b'{"buffer":0,"byteOffset":36,"byteLength":6}],'
                    b'"accessors":[{"bufferView":0,"componentType":5126,"count":3,'
                    b'"type":"VEC3"},{"bufferView":1,"componentType":5123,'
                    b'"count":3,"type":"SCALAR"}],'
                    b'"meshes":[{"primitives":[{"attributes":{"POSITION":0},'
                    b'"indices":1}]}],"nodes":[{"mesh":0}],'
                    b'"scenes":[{"nodes":[0]}],"scene":0}'
                )
                asset_path = Path(stream.name)
            try:
                first = client.load_asset(asset_path)
                second = client.load_asset(asset_path)
                self.assertFalse(first.reused)
                self.assertEqual(first.asset.adapter, "cgltf/1.15")
                self.assertEqual(first.asset.primitives[0].positions.element_count, 3)
                self.assertEqual(
                    first.asset.canonical_geometry_fingerprint.triangle_count,
                    1,
                )
                self.assertTrue(second.reused)
                self.assertEqual(first.asset.id, second.asset.id)
                self.assertEqual(client.release_asset(first.asset).remaining_references, 1)
                self.assertTrue(client.release_asset(second.asset).released)
                with self.assertRaises(RuntimeRPCError) as raised:
                    client.release_asset(first.asset)
                self.assertEqual(raised.exception.data["code"], "STALE_HANDLE")
            finally:
                asset_path.unlink(missing_ok=True)
        finally:
            client.shutdown()

    @unittest.skipUnless(sys.platform == "win32", "Windows Named Pipe test")
    def test_named_pipe_client_reaches_the_same_dispatcher(self) -> None:
        pipe_name = rf"\\.\pipe\Unified3D.Runtime.test.{uuid.uuid4().hex}"
        server = subprocess.Popen([str(self.executable), "--pipe", pipe_name])
        client: Unified3DClient | None = None
        try:
            client = Unified3DClient(
                NamedPipeTransport.connect(pipe_name, timeout=5.0)
            )
            self.assertEqual(client.hello()["protocol_version"], "1.0")
            client.shutdown()
            client = None
            self.assertEqual(server.wait(timeout=5.0), 0)
        finally:
            if client is not None:
                client.close()
            if server.poll() is None:
                server.terminate()
                server.wait(timeout=5.0)


if __name__ == "__main__":
    unittest.main()
