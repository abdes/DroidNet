// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Activation;

/// <summary>
/// Workspace restoration request attached to project activation.
/// </summary>
public sealed record WorkspaceRestorationRequest
{
    /// <summary>
    /// Gets the project identity whose persisted workspace state should be restored.
    /// </summary>
    public required Guid ProjectId { get; init; }

    /// <summary>
    /// Gets the project root path.
    /// </summary>
    public required string ProjectRoot { get; init; }
}
