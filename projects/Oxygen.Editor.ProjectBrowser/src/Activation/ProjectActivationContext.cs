// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ProjectBrowser.Activation;

/// <summary>
/// Shell-side context produced after a project has been activated.
/// </summary>
public sealed record ProjectActivationContext
{
    /// <summary>
    /// Gets the project-services-owned project context.
    /// </summary>
    public required ProjectContext ProjectContext { get; init; }

    /// <summary>
    /// Gets the usage record identity associated with the request, when available.
    /// </summary>
    public string? RecentEntryId { get; init; }

    /// <summary>
    /// Gets the workspace restoration request.
    /// </summary>
    public required WorkspaceRestorationRequest RestorationRequest { get; init; }
}
