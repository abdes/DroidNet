"""Own Windows child processes and their descendants."""

import ctypes
import subprocess

ERROR_ACCESS_DENIED = 5


class WindowsJob:
    """Own the child process tree without invoking taskkill or WMI."""

    def __init__(self, process: subprocess.Popen) -> None:
        from ctypes import wintypes

        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
        self.kernel.CreateJobObjectW.restype = wintypes.HANDLE
        self.kernel.AssignProcessToJobObject.argtypes = [
            wintypes.HANDLE,
            wintypes.HANDLE,
        ]
        self.kernel.AssignProcessToJobObject.restype = wintypes.BOOL
        self.kernel.TerminateJobObject.argtypes = [wintypes.HANDLE, wintypes.UINT]
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.handle = self.kernel.CreateJobObjectW(None, None)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        if not self.kernel.AssignProcessToJobObject(self.handle, int(process._handle)):
            error = ctypes.get_last_error()
            # Windows refuses to assign a process that has already exited, which
            # a short-lived child can do before this call. It owns no tree then.
            if not (error == ERROR_ACCESS_DENIED and process.poll() is not None):
                self.close()
                raise ctypes.WinError(error)

    def terminate(self) -> None:
        self.kernel.TerminateJobObject(self.handle, 130)

    def close(self) -> None:
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None
