// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Cooking;

/// <summary>What a cook run's panel does around a cook: saving what it reads, and showing where its issues and outputs are.</summary>
public interface ICookRecovery
{
    /// <summary>Opens and focuses the exact property identified by a cook issue.</summary>
    /// <param name="run">The originating run and project.</param>
    /// <param name="issue">The structured property target.</param>
    /// <returns>Whether the target was accepted for navigation.</returns>
    public Task<bool> GoToPropertyAsync(CookRunSnapshot run, DiagnosticRecord issue);

    /// <summary>Saves the unsaved documents a cook reads, after asking.</summary>
    /// <param name="request">The cook.</param>
    /// <returns>Whether the cook can run: nothing it reads is unsaved, or every document was saved.</returns>
    public Task<bool> SaveInputsAsync(CookRunRequest request);

    /// <summary>Opens a read-only report of the current published output for a completed cook's scope.</summary>
    /// <param name="run">The selected cook and its original scope.</param>
    /// <returns>Whether the report was opened.</returns>
    public Task<bool> InspectAsync(CookRunSnapshot run);

    /// <summary>Reveals the selected run's imported outputs in the Content Explorer on explicit request.</summary>
    /// <param name="run">The successful run and its actual output identities.</param>
    /// <returns>Whether available output assets were revealed.</returns>
    public Task<bool> ShowImportedAssetsAsync(CookRunSnapshot run);
}
