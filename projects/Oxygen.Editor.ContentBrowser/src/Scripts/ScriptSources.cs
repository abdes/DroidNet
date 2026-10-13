// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.Scripts;

/// <summary>
/// Recognizes hand-written script and input sources and maps them to the assets they cook to.
/// </summary>
/// <remarks>
/// A script is identified by its source location: <c>Content/Player/move.lua</c> cooks to
/// <c>/Content/Player/move.oscript</c>. Input files keep their own identity; the actions and
/// contexts they declare cook next to them.
/// </remarks>
public static class ScriptSources
{
    /// <summary>The cooked script extension.</summary>
    public const string CookedScriptExtension = ".oscript";

    /// <summary>The extension of a mapping-context input file.</summary>
    public const string InputFileSuffix = ".input.json";

    /// <summary>The extension of a single-action input file.</summary>
    public const string InputActionFileSuffix = ".input-action.json";

    /// <summary>The extension New Script writes.</summary>
    public const string ScriptExtension = ".lua";

    private static readonly string[] ScriptExtensions = [".lua", ".luau"];

    /// <summary>Checks whether a path or virtual path names a Luau script source.</summary>
    /// <param name="path">The path.</param>
    /// <returns><see langword="true"/> for <c>.lua</c> and <c>.luau</c> files.</returns>
    public static bool IsScriptSource(string path)
        => ScriptExtensions.Any(extension => path.EndsWith(extension, StringComparison.OrdinalIgnoreCase));

    /// <summary>Checks whether a path or virtual path names a hand-written input file.</summary>
    /// <param name="path">The path.</param>
    /// <returns><see langword="true"/> for <c>*.input.json</c> and <c>*.input-action.json</c> files.</returns>
    public static bool IsInputSource(string path)
        => path.EndsWith(InputFileSuffix, StringComparison.OrdinalIgnoreCase)
           || path.EndsWith(InputActionFileSuffix, StringComparison.OrdinalIgnoreCase);

    /// <summary>Gets the virtual path a script source cooks to.</summary>
    /// <param name="sourceVirtualPath">The script source virtual path, such as <c>/Content/Player/move.lua</c>.</param>
    /// <returns>The cooked virtual path, such as <c>/Content/Player/move.oscript</c>.</returns>
    public static string ToCookedScriptPath(string sourceVirtualPath)
        => StripSourceExtension(sourceVirtualPath) + CookedScriptExtension;

    /// <summary>Removes a script or input file extension from a file name or path.</summary>
    /// <param name="path">The file name or path.</param>
    /// <returns>The name without its source extension; other names are returned unchanged.</returns>
    public static string StripSourceExtension(string path)
    {
        foreach (var suffix in (string[])[InputActionFileSuffix, InputFileSuffix, .. ScriptExtensions])
        {
            if (path.EndsWith(suffix, StringComparison.OrdinalIgnoreCase))
            {
                return path[..^suffix.Length];
            }
        }

        return path;
    }
}
