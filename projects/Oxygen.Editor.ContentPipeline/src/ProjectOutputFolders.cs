// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Names the folders the content pipeline writes inside a project: cooked, imported and build output.</summary>
public static class ProjectOutputFolders
{
    /// <summary>Checks whether a project mount is one of the pipeline's output folders rather than authored content.</summary>
    /// <param name="mount">The project mount.</param>
    /// <returns><see langword="true"/> for the <c>.cooked</c>, <c>.imported</c> and <c>.build</c> folders.</returns>
    public static bool Contains(ProjectMountPoint mount)
    {
        ArgumentNullException.ThrowIfNull(mount);
        var relativePath = mount.RelativePath.Trim().Replace('\\', '/').Trim('/');
        return string.Equals(relativePath, ".cooked", StringComparison.OrdinalIgnoreCase)
               || string.Equals(relativePath, ".imported", StringComparison.OrdinalIgnoreCase)
               || string.Equals(relativePath, ".build", StringComparison.OrdinalIgnoreCase);
    }
}
