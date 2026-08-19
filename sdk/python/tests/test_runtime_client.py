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
        self.assertFalse(loaded.reused)

    def test_local_comparator_remains_the_named_oracle(self) -> None:
        self.assertIs(compare_analyses_oracle, compare_analyses)


def runtime_executable() -> Path | None:
    configured = os.environ.get("UNIFIED3D_RUNTIME_EXECUTABLE")
    executable = (
        Path(configured)
        if configured
        else REPOSITORY_ROOT / "build" / "dev-gcc" / "runtime" / "unified3d-runtime.exe"
    )
    return executable if executable.is_file() else None


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
