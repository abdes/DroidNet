// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Runtime.InteropServices;

public static class PixLoader
{
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr LoadLibrary(string dllToLoad);

    public static void LoadPixDll()
    {
#if DEBUG
        string dllName = "C:\\Program Files\\Microsoft PIX\\2509.25\\WinPixGpuCapturer.dll";
        IntPtr handle = LoadLibrary(dllName);
        if (handle == IntPtr.Zero)
        {
            int errorCode = Marshal.GetLastWin32Error();
            Console.WriteLine($"Failed to load {dllName}. Error code: {errorCode}");
        }
        else
        {
            Console.WriteLine($"{dllName} loaded successfully.");
        }
#endif
    }
}
