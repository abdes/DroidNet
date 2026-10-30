// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Resolved target folder for creating an authored asset.
/// </summary>
/// <param name="MountName">The mount token used in asset URIs.</param>
/// <param name="Folder">The target folder's virtual path.</param>
/// <param name="IsExplicitLocalMount">Whether the target came from a mount the selection names.</param>
/// <param name="FallbackReason">The reason a selected folder was ignored, if any.</param>
public sealed record AuthoringTarget(
    string MountName,
    AssetPath Folder,
    bool IsExplicitLocalMount,
    AuthoringTargetFallbackReason? FallbackReason);
