// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using Oxygen.Editor.Interop.Testing;

namespace Oxygen.Editor.Interop.Tests;

/// <summary>Points the native DLL loader at the installed engine SDK before any test loads the interop assembly.</summary>
internal static class NativeLoader
{
    /// <summary>Registers the engine SDK's native DLL folder.</summary>
    [ModuleInitializer]
    internal static void Configure() => NativeDllSearchPath.Configure();
}
