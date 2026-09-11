// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Cooking;

/// <summary>Routes cook recovery through the workspace's document and inspector owners.</summary>
public interface ICookingWorkspaceActions
{
    /// <summary>Opens and focuses the exact property identified by a cook issue.</summary>
    /// <param name="run">The originating run and project.</param>
    /// <param name="issue">The structured property target.</param>
    /// <returns>Whether the target was accepted for navigation.</returns>
    public Task<bool> GoToPropertyAsync(CookRunSnapshot run, DiagnosticRecord issue);

    /// <summary>Saves exactly the documents captured in the displayed blocked request.</summary>
    /// <param name="run">The immutable displayed request and its explicit document list.</param>
    /// <returns>Whether ordinary saves completed with no remaining dirty documents.</returns>
    public Task<bool> SaveListedAsync(CookRunSnapshot run);

    /// <summary>Activates a listed open document without hiding Cooking.</summary>
    /// <param name="documentId">The listed document identity.</param>
    /// <returns>Whether the document could be activated.</returns>
    public Task<bool> OpenDocumentAsync(Guid documentId);
}
