// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// Script sources are identified by folder and name without extension: <c>move.lua</c> and <c>move.luau</c> in one
/// folder would cook to the same script, so no relocation may create that pair.
/// </summary>
internal static class ScriptSourceNames
{
    private static readonly string[] Extensions = [".lua", ".luau"];

    /// <summary>Finds an existing script source that a script written at <paramref name="target"/> would collide with.</summary>
    /// <param name="target">The physical path a script would be written to.</param>
    /// <param name="ignored">A path that does not count as existing, such as the source of a rename.</param>
    /// <returns>The colliding file, or <see langword="null"/> when <paramref name="target"/> is not a script or has no collision.</returns>
    public static string? FindCollision(string target, string? ignored = null)
    {
        var extension = Extensions.FirstOrDefault(candidate => target.EndsWith(candidate, StringComparison.OrdinalIgnoreCase));
        if (extension is null)
        {
            return null;
        }

        var stem = target[..^extension.Length];
        return Extensions
            .Where(other => !string.Equals(other, extension, StringComparison.OrdinalIgnoreCase))
            .Select(other => stem + other)
            .FirstOrDefault(other => File.Exists(other) && !string.Equals(other, ignored, StringComparison.OrdinalIgnoreCase));
    }

    /// <summary>Gets the identity that references to a script source use: the script it cooks to.</summary>
    /// <param name="virtualPath">A virtual path such as <c>/Content/Player/move.lua</c>.</param>
    /// <returns>The script identity, such as <c>/Content/Player/move.oscript</c>, or <see langword="null"/> when the
    /// path is not a script source.</returns>
    public static string? ToScriptIdentity(string virtualPath)
    {
        ArgumentNullException.ThrowIfNull(virtualPath);
        var extension = Extensions.FirstOrDefault(candidate => virtualPath.EndsWith(candidate, StringComparison.OrdinalIgnoreCase));
        return extension is null ? null : virtualPath[..^extension.Length] + ".oscript";
    }
}
