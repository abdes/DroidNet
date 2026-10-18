// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>Names the folders the content pipeline writes inside a project: cooked, imported and build output.</summary>
public static class ProjectOutputFolders
{
    /// <summary>
    /// Checks whether a project mount is one of the pipeline's output folders rather than authored content: a folder
    /// whose name starts with a dot, such as <c>.cooked</c>, <c>.imported</c> or <c>.build</c>. <c>SourceMedia</c>,
    /// which the virtual path rules also keep out of authored paths, holds sources and is not output.
    /// </summary>
    /// <param name="mount">The project mount.</param>
    /// <returns><see langword="true"/> for a mount in a pipeline output folder.</returns>
    public static bool Contains(ProjectMountPoint mount)
    {
        ArgumentNullException.ThrowIfNull(mount);
        return mount.RelativePath.Trim().Replace('\\', '/').Trim('/').Split('/').Any(static segment => segment.StartsWith('.'));
    }
}
