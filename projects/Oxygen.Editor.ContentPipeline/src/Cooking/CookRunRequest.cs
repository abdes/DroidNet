// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>Identifies the logical scope of a cook independently of its captured inputs.</summary>
/// <param name="TargetKind">Whether the request cooks an asset, scene, folder, the project, or several named assets.</param>
/// <param name="ScopeUri">The source asset/folder identity; null for the entire project or several assets.</param>
/// <param name="IsAutomatic">Whether the request must remain quiet in the workspace.</param>
public sealed record CookRunRequest(CookTargetKind TargetKind, AssetPath? ScopeUri, bool IsAutomatic = false)
{
    /// <summary>Gets the assets a <see cref="CookTargetKind.Changes"/> request cooks.</summary>
    public IReadOnlyList<AssetPath> Assets { get; init; } = [];

    /// <summary>Gets an initial or partially retained import request used by Retry.</summary>
    public Import.SceneImportRequest? Import { get; init; }

    /// <summary>Gets a value indicating whether the request explicitly reimports retained source.</summary>
    public bool IsReimport { get; init; }

    /// <summary>Gets a value indicating whether active preview needs this request ahead of background saves.</summary>
    public bool IsDemand { get; init; }

    /// <summary>Gets a value indicating whether equivalent pending scopes can share work before input capture.</summary>
    public bool CoalescePending { get; init; }

    /// <summary>Gets the originating context so coalescing cannot bypass a caller's project check.</summary>
    internal Projects.ProjectContext? OriginContext { get; init; }
}
