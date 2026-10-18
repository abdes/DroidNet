// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects;

/// <summary>A file or folder of the project, by its virtual path and on disk.</summary>
/// <param name="Path">The virtual path, such as <c>/Content/Materials/Wood.omat.json</c>.</param>
/// <param name="File">The file or folder on disk.</param>
/// <param name="MountRoot">The folder on disk that the mount names.</param>
/// <param name="MountKind">Whether the mount holds the project's sources or a local library.</param>
public sealed record ProjectAssetLocation(
    AssetPath Path,
    FileSystemPath File,
    FileSystemPath MountRoot,
    ProjectMountKind MountKind)
{
    /// <summary>Gets a value indicating whether the location is the mount's folder itself.</summary>
    public bool IsMountRoot => this.Path.IsMount;

    /// <summary>Gets the identity of the asset this file is the source of, such as <c>/Content/Materials/Wood.omat</c>.</summary>
    public AssetPath Identity => AssetFileTypes.ToIdentity(this.Path);
}
