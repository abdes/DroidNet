// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// Represents a single import input.
/// </summary>
/// <param name="SourcePath">
/// Project-relative source path using <c>/</c> separators (for example <c>Content/Textures/Wood.png</c>).
/// </param>
/// <param name="MountPoint">The authoring mount point (for example <c>Content</c>).</param>
/// <param name="VirtualPath">
/// Optional explicit virtual path. If not provided, the import pipeline derives one from the source path.
/// </param>
/// <param name="Settings">
/// Optional importer-specific settings (for example <c>ExtractMaterials=true</c>).
/// </param>
public sealed record ImportInput(
    string SourcePath,
    string MountPoint,
    string? VirtualPath = null,
    IReadOnlyDictionary<string, string>? Settings = null);
