// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Resolved scope for an explicit content cook operation.
/// </summary>
/// <param name="Project">The active project context.</param>
/// <param name="CookScope">The project cook scope.</param>
/// <param name="Inputs">The authored inputs included in the cook.</param>
/// <param name="TargetKind">The user-visible cook target kind.</param>
public sealed record ContentCookScope(
    ProjectContext Project,
    ProjectCookScope CookScope,
    IReadOnlyList<ContentCookInput> Inputs,
    CookTargetKind TargetKind)
{
    /// <summary>Gets the coherent saved input set when preparation belongs to a cook operation.</summary>
    public CookInputSnapshot? Snapshot { get; init; }

    /// <summary>Gets the compatible artifacts borrowed from the owning cook operation.</summary>
    public NativeArtifactLease? Artifacts { get; init; }

    /// <summary>Gets the physical input root used by descriptor generation and native jobs.</summary>
    public string InputRoot => this.Snapshot?.InputRoot ?? this.Project.ProjectRoot;
}
