// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

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
    CookTargetKind TargetKind);
