// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
///     Resolved target folder for creating an authored asset.
/// </summary>
/// <param name="MountName">The mount token used in asset URIs.</param>
/// <param name="ProjectRelativeFolder">The project-relative folder path for project authoring mounts.</param>
/// <param name="FolderAssetUri">The asset URI for the target folder.</param>
/// <param name="IsExplicitLocalMount">Whether the target came from an explicitly selected local mount.</param>
/// <param name="FallbackReason">The reason a selected folder was ignored, if any.</param>
public sealed record AuthoringTarget(
    string MountName,
    string ProjectRelativeFolder,
    Uri FolderAssetUri,
    bool IsExplicitLocalMount,
    AuthoringTargetFallbackReason? FallbackReason);
