// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>Supplies source facts from owned native analysis or accepted publication data.</summary>
internal interface ICookSourceFactsProvider
{
    /// <summary>Reads dependency facts for one unresolved input frontier.</summary>
    /// <param name="inputs">The authored inputs to analyze or resolve from accepted facts.</param>
    /// <param name="cancellationToken">Cancels source discovery.</param>
    /// <returns>The source facts, generated inputs and diagnostics for this frontier.</returns>
    public Task<CookSourceFrontier> ReadAsync(IReadOnlyList<ContentCookInput> inputs, CancellationToken cancellationToken);
}
