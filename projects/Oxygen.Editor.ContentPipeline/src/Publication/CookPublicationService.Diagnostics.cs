// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>The diagnostics each published product keeps, read from the selected publication.</summary>
public sealed partial class CookPublicationService : IPublishedDiagnostics
{
    /// <inheritdoc />
    public async Task<ImmutableArray<DiagnosticRecord>> ReadDiagnosticsAsync(ProjectContext project, AssetPath source, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(source);
        using var publication = await this.AcquireReadAsync(project, cancellationToken).ConfigureAwait(false);
        return publication.ProductState.Products.FirstOrDefault(product => product.SourceUri == source)?.Diagnostics ?? [];
    }
}
