// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.World.Inspection;

/// <summary>Session-only metadata for an explicitly opened read-only cooked-output report.</summary>
/// <param name="project">The originating project activation.</param>
/// <param name="scopeUri">The requested scope.</param>
/// <param name="validate">Whether initial inspection should check root integrity.</param>
public sealed class CookedInspectionDocumentMetadata(ProjectContext project, Uri? scopeUri, bool validate) : BaseDocumentMetadata
{
    /// <summary>Occurs when an explicit request refreshes this report.</summary>
    public event EventHandler? RefreshRequested;

    /// <inheritdoc />
    public override string DocumentType => "CookedInspection";

    /// <summary>Gets the originating project.</summary>
    public ProjectContext Project { get; } = project;

    /// <summary>Gets the inspected scope.</summary>
    public Uri? ScopeUri { get; } = scopeUri;

    /// <summary>Gets a value indicating whether the current request includes integrity validation.</summary>
    public bool ValidateRequested { get; private set; } = validate;

    /// <summary>Requests a fresh report without changing authoring metadata or dirty state.</summary>
    /// <param name="validate">Whether to include validation.</param>
    public void RequestRefresh(bool validate)
    {
        this.ValidateRequested = validate;
        this.RefreshRequested?.Invoke(this, EventArgs.Empty);
    }
}
