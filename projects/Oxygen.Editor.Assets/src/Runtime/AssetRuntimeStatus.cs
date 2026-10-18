// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Assets.Runtime;

/// <summary>The preview's acknowledged cooked content and the outcomes of one project's asset requests.</summary>
/// <param name="State">The state of the preview's cooked content.</param>
/// <param name="Roots">The mounted cooked roots, as physical paths.</param>
/// <param name="Requests">The asset requests of the current run for the requested project.</param>
/// <param name="Reason">Why content is unavailable, when the preview reported it.</param>
public sealed record AssetRuntimeStatus(
    AssetRuntimeContentState State,
    IReadOnlyList<FileSystemPath> Roots,
    IReadOnlyList<AssetRuntimeRequest> Requests,
    string? Reason = null)
{
    /// <summary>Gets the status of a preview that is not running.</summary>
    public static AssetRuntimeStatus Unavailable { get; } = new(AssetRuntimeContentState.Unavailable, [], []);
}
