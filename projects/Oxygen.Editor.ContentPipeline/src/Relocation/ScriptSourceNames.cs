// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>
/// Script sources are identified by folder and name without extension: <c>move.lua</c> and <c>move.luau</c> in one
/// folder would cook to the same script, so no relocation may create that pair.
/// </summary>
internal static class ScriptSourceNames
{
    private static readonly IReadOnlyList<string> Extensions = AssetFileTypes.Suffixes(AssetFileCategory.Script);

    /// <summary>Finds an existing script source that a script written at <paramref name="target"/> would collide with.</summary>
    /// <param name="target">The location a script would be written to.</param>
    /// <param name="ignored">A location that does not count as existing, such as the source of a rename.</param>
    /// <returns>The colliding file, or <see langword="null"/> when <paramref name="target"/> is not a script or has no collision.</returns>
    public static FileSystemPath? FindCollision(FileSystemPath target, FileSystemPath? ignored = null)
    {
        ArgumentNullException.ThrowIfNull(target);
        var extension = Extensions.FirstOrDefault(candidate => target.Name.EndsWith(candidate, StringComparison.OrdinalIgnoreCase));
        if (extension is null)
        {
            return null;
        }

        var stem = target.FullPath[..^extension.Length];
        return Extensions
            .Where(other => !string.Equals(other, extension, StringComparison.OrdinalIgnoreCase))
            .Select(other => FileSystemPath.From(stem + other))
            .FirstOrDefault(other => File.Exists(other.FullPath) && other != ignored);
    }
}
