"""Synchronous Python client for the native Unified3D Runtime."""

from __future__ import annotations

from itertools import count
import json
import os
from pathlib import Path
from threading import RLock
from typing import Any

from ..analysis import AnalysisRecord, canonicalize_analysis
from .models import (
    AssetHandle,
    LoadAssetResult,
    ReleaseAssetResult,
    RuntimeComparisonResult,
)
from .transports import DEFAULT_WINDOWS_PIPE, NamedPipeTransport, RuntimeTransport, StdioTransport


class RuntimeRPCError(RuntimeError):
    """Structured JSON-RPC error returned by the native Runtime."""

    def __init__(self, code: int, message: str, data: Any = None) -> None:
        super().__init__(f"Unified3D Runtime error {code}: {message}")
        self.code = code
        self.message = message
        self.data = data


class Unified3DClient:
    """Typed, UI-independent facade over the native JSON-RPC Runtime."""

    def __init__(self, transport: RuntimeTransport) -> None:
        self._transport = transport
        self._ids = count(1)
        self._lock = RLock()
        self._closed = False

    @classmethod
    def connect_stdio(
        cls,
        executable: str | os.PathLike[str],
    ) -> "Unified3DClient":
        return cls(StdioTransport.launch(executable))

    @classmethod
    def connect_named_pipe(
        cls,
        pipe_name: str = DEFAULT_WINDOWS_PIPE,
        *,
        timeout: float = 5.0,
    ) -> "Unified3DClient":
        return cls(NamedPipeTransport.connect(pipe_name, timeout=timeout))

    def request(self, method: str, params: dict[str, Any] | None = None) -> Any:
        with self._lock:
            if self._closed:
                raise RuntimeError("Unified3DClient is closed")
            request_id = next(self._ids)
            payload: dict[str, Any] = {
                "jsonrpc": "2.0",
                "id": request_id,
                "method": method,
            }
            if params is not None:
                payload["params"] = params
            encoded = json.dumps(
                payload,
                ensure_ascii=False,
                separators=(",", ":"),
            ).encode("utf-8")
            response = json.loads(self._transport.exchange(encoded).decode("utf-8"))
            if not isinstance(response, dict):
                raise ConnectionError("Runtime returned a non-object JSON-RPC response")
            if response.get("jsonrpc") != "2.0" or response.get("id") != request_id:
                raise ConnectionError("Runtime returned a mismatched JSON-RPC response")
            if "error" in response:
                error = response["error"]
                raise RuntimeRPCError(
                    int(error["code"]),
                    str(error["message"]),
                    error.get("data"),
                )
            if "result" not in response:
                raise ConnectionError("Runtime response contains neither result nor error")
            return response["result"]

    def hello(self) -> dict[str, Any]:
        return self.request("runtime.hello")

    def validate_analysis(
        self,
        analysis: AnalysisRecord | dict[str, Any],
    ) -> dict[str, Any]:
        value = analysis.to_dict() if isinstance(analysis, AnalysisRecord) else analysis
        return self.request("analysis.validate", {"analysis": value})

    def compare_analyses(
        self,
        analysis_a: str | dict[str, Any] | AnalysisRecord,
        analysis_b: str | dict[str, Any] | AnalysisRecord,
    ) -> RuntimeComparisonResult:
        canonical_a = (
            analysis_a
            if isinstance(analysis_a, AnalysisRecord)
            else canonicalize_analysis(analysis_a)
        )
        canonical_b = (
            analysis_b
            if isinstance(analysis_b, AnalysisRecord)
            else canonicalize_analysis(analysis_b)
        )
        result = self.request(
            "analysis.compare",
            {"a": canonical_a.to_dict(), "b": canonical_b.to_dict()},
        )
        return RuntimeComparisonResult(payload=result)

    def load_asset(
        self,
        path: str | os.PathLike[str],
        *,
        backend: str = "auto",
    ) -> LoadAssetResult:
        if backend not in {"auto", "cgltf", "ufbx", "autodesk_fbx"}:
            raise ValueError("backend must be auto, cgltf, ufbx, or autodesk_fbx")
        result = self.request(
            "asset.load",
            {"path": str(Path(path).resolve()), "backend": backend},
        )
        return LoadAssetResult(
            asset=AssetHandle.from_dict(result["asset"]),
            reused=bool(result["reused"]),
        )

    def release_asset(self, asset: AssetHandle) -> ReleaseAssetResult:
        result = self.request("asset.release", {"asset": asset.to_wire()})
        return ReleaseAssetResult(
            released=bool(result["released"]),
            remaining_references=int(result["remaining_references"]),
        )

    def shutdown(self) -> None:
        with self._lock:
            if self._closed:
                return
            try:
                self.request("runtime.shutdown")
            finally:
                self._transport.close()
                self._closed = True

    def close(self) -> None:
        with self._lock:
            if self._closed:
                return
            self._transport.close()
            self._closed = True

    def __enter__(self) -> "Unified3DClient":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()
