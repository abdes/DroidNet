// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>Where an asset of the project is: its identity, and its file or folder under one of the project's mounts.</summary>
/// <param name="AssetUri">The asset URI, in the form the asset catalog writes.</param>
/// <param name="VirtualPath">The virtual path: the mount name, then the path inside the mount, such as <c>/Content/Wood.omat.json</c>.</param>
/// <param name="FilePath">The absolute path of the file or folder.</param>
/// <param name="MountName">The name of the mount, as the project declares it.</param>
/// <param name="MountRoot">The absolute path of the mount's folder.</param>
/// <param name="MountRelativePath">The path inside the mount, with <c>/</c> separators; empty for the mount's folder.</param>
/// <param name="MountKind">The kind of mount.</param>
public sealed record ProjectAssetLocation(
    Uri AssetUri,
    string VirtualPath,
    string FilePath,
    string MountName,
    string MountRoot,
    string MountRelativePath,
    ProjectMountKind MountKind)
{
    /// <summary>Gets a value indicating whether the location is the mount's folder itself.</summary>
    public bool IsMountRoot => this.MountRelativePath.Length == 0;
}
