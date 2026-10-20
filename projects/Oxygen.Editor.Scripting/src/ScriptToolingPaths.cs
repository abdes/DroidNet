// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.Scripting;

/// <summary>
/// Locates the scripting files the engine SDK ships: the Luau API definitions and the input file schemas.
/// </summary>
/// <param name="DefinitionsPath">The SDK's <c>oxygen.d.luau</c>.</param>
/// <param name="SchemaDirectory">The SDK's JSON schema folder.</param>
public sealed record ScriptToolingPaths(string DefinitionsPath, string SchemaDirectory)
{
    /// <summary>Finds the files of the SDK this editor runs against.</summary>
    /// <returns>The SDK scripting paths; the files may be missing from an older SDK.</returns>
    public static ScriptToolingPaths Discover()
    {
        var installation = EditorNativeInstallation.Discover(
            AppContext.BaseDirectory,
#if DEBUG
            "Debug");
#else
            "Release");
#endif
        return new(
            Path.Combine(installation.EngineRoot, "share", "oxygen", "scripting", "oxygen.d.luau"),
            installation.SchemaDirectory);
    }
}
