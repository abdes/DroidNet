// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

// The Windows App SDK compiles its registration-free WinRT initializer into every unpackaged test app. Its one
// P/Invoke loads the runtime the SDK deploys beside the app; the file is the SDK's and cannot be changed here.
[assembly: SuppressMessage(
    "Security",
    "CA5392:Use DefaultDllImportSearchPaths attribute for P/Invokes",
    Justification = "The Windows App SDK's own initializer, compiled into the test app; it loads the runtime the SDK deploys beside the app.",
    Scope = "member",
    Target = "~M:Microsoft.Windows.Foundation.UndockedRegFreeWinRTCS.NativeMethods.WindowsAppRuntime_EnsureIsLoaded~System.Int32")]
