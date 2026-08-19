"""JSON-line transports for the native Unified3D Runtime."""

from __future__ import annotations

from abc import ABC, abstractmethod
import os
from pathlib import Path
import subprocess
import sys
import time
from typing import BinaryIO, Sequence


DEFAULT_WINDOWS_PIPE = r"\\.\pipe\Unified3D.Runtime.v1"


class RuntimeTransport(ABC):
    """Synchronous request/response transport used by :class:`Unified3DClient`."""

    @abstractmethod
    def exchange(self, request: bytes) -> bytes:
        """Send one JSON line and return one JSON response line."""

    @abstractmethod
    def close(self) -> None:
        """Release transport resources without sending an RPC method."""


class StdioTransport(RuntimeTransport):
    """Own a Runtime child process using newline-delimited stdin/stdout."""

    def __init__(self, process: subprocess.Popen[bytes]) -> None:
        if process.stdin is None or process.stdout is None:
            raise ValueError("Runtime process must expose stdin and stdout pipes")
        self._process = process
        self._stdin: BinaryIO = process.stdin
        self._stdout: BinaryIO = process.stdout

    @classmethod
    def launch(
        cls,
        executable: str | os.PathLike[str],
        *,
        extra_args: Sequence[str] = (),
    ) -> "StdioTransport":
        command = [str(Path(executable)), "--stdio", *extra_args]
        process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=None,
            bufsize=0,
        )
        return cls(process)

    @property
    def process(self) -> subprocess.Popen[bytes]:
        return self._process

    def exchange(self, request: bytes) -> bytes:
        if self._process.poll() is not None:
            raise ConnectionError(
                f"Unified3D Runtime exited with code {self._process.returncode}"
            )
        self._stdin.write(request + b"\n")
        self._stdin.flush()
        response = self._stdout.readline()
        if not response:
            raise ConnectionError("Unified3D Runtime closed stdout before responding")
        return response.rstrip(b"\r\n")

    def close(self) -> None:
        try:
            if self._process.poll() is None:
                self._stdin.close()
                try:
                    self._process.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    self._process.terminate()
                    try:
                        self._process.wait(timeout=2.0)
                    except subprocess.TimeoutExpired:
                        self._process.kill()
                        self._process.wait(timeout=2.0)
        finally:
            try:
                if not self._stdin.closed:
                    self._stdin.close()
            finally:
                if not self._stdout.closed:
                    self._stdout.close()


class NamedPipeTransport(RuntimeTransport):
    """Connect to a local Windows Named Pipe without a third-party dependency."""

    def __init__(self, handle: int, *, kernel32: object) -> None:
        self._handle = handle
        self._kernel32 = kernel32

    @classmethod
    def connect(
        cls,
        pipe_name: str = DEFAULT_WINDOWS_PIPE,
        *,
        timeout: float = 5.0,
    ) -> "NamedPipeTransport":
        if sys.platform != "win32":
            raise OSError("Windows Named Pipes are available only on Windows")
        if not pipe_name.startswith("\\\\.\\pipe\\"):
            raise ValueError("Only local Windows Named Pipe names are accepted")

        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CreateFileW.argtypes = [
            wintypes.LPCWSTR,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.LPVOID,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.HANDLE,
        ]
        kernel32.CreateFileW.restype = wintypes.HANDLE
        kernel32.WaitNamedPipeW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD]
        kernel32.WaitNamedPipeW.restype = wintypes.BOOL
        kernel32.ReadFile.argtypes = [
            wintypes.HANDLE,
            wintypes.LPVOID,
            wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD),
            wintypes.LPVOID,
        ]
        kernel32.ReadFile.restype = wintypes.BOOL
        kernel32.WriteFile.argtypes = [
            wintypes.HANDLE,
            ctypes.c_void_p,
            wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD),
            wintypes.LPVOID,
        ]
        kernel32.WriteFile.restype = wintypes.BOOL
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel32.CloseHandle.restype = wintypes.BOOL

        generic_read = 0x80000000
        generic_write = 0x40000000
        open_existing = 3
        error_file_not_found = 2
        error_pipe_busy = 231
        invalid_handle = ctypes.c_void_p(-1).value
        deadline = time.monotonic() + timeout

        while True:
            handle = kernel32.CreateFileW(
                pipe_name,
                generic_read | generic_write,
                0,
                None,
                open_existing,
                0,
                None,
            )
            numeric_handle = int(handle) if handle is not None else invalid_handle
            if numeric_handle != invalid_handle:
                return cls(numeric_handle, kernel32=kernel32)
            error = ctypes.get_last_error()
            remaining = deadline - time.monotonic()
            if error not in {error_file_not_found, error_pipe_busy} or remaining <= 0:
                raise OSError(error, f"Cannot connect to Named Pipe {pipe_name}")
            if error == error_pipe_busy:
                kernel32.WaitNamedPipeW(
                    pipe_name,
                    max(1, min(100, int(remaining * 1000))),
                )
            else:
                time.sleep(min(0.01, remaining))

    def exchange(self, request: bytes) -> bytes:
        import ctypes
        from ctypes import wintypes

        payload = request + b"\n"
        offset = 0
        while offset < len(payload):
            chunk = payload[offset:]
            buffer = ctypes.create_string_buffer(chunk)
            written = wintypes.DWORD()
            if not self._kernel32.WriteFile(
                self._handle,
                buffer,
                len(chunk),
                ctypes.byref(written),
                None,
            ):
                raise OSError(ctypes.get_last_error(), "Named Pipe write failed")
            if written.value == 0:
                raise ConnectionError("Named Pipe write produced zero bytes")
            offset += written.value

        response = bytearray()
        while True:
            buffer = ctypes.create_string_buffer(64 * 1024)
            received = wintypes.DWORD()
            if not self._kernel32.ReadFile(
                self._handle,
                buffer,
                len(buffer),
                ctypes.byref(received),
                None,
            ):
                raise OSError(ctypes.get_last_error(), "Named Pipe read failed")
            response.extend(buffer.raw[: received.value])
            newline = response.find(b"\n")
            if newline >= 0:
                return bytes(response[:newline]).rstrip(b"\r")

    def close(self) -> None:
        if self._handle is None:
            return
        self._kernel32.CloseHandle(self._handle)
        self._handle = None
