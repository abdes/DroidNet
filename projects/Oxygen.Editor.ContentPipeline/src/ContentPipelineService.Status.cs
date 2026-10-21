// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Exposes read-only cook facts through the existing shared pipeline service.</summary>
public sealed partial class ContentPipelineService
{
    private readonly AssetCookStatusReader statusReader = new(publication, nativeCompatibility, engineContent);

    /// <inheritdoc />
    public event EventHandler? Changed
    {
        add => this.nativeCompatibility.ObservationChanged += value;
        remove => this.nativeCompatibility.ObservationChanged -= value;
    }

    /// <inheritdoc />
    public Task<IReadOnlyList<AssetCookStatus>> ReadAsync(ProjectContext project, IReadOnlyList<AssetPath> assetUris, CancellationToken cancellationToken = default)
        => this.statusReader.ReadAsync(project, assetUris, cancellationToken);

    /// <inheritdoc />
    public Task<IReadOnlyList<AssetCookStatus>> ReadAsync(ProjectContext project, Publication.ICookPublicationReadLease publication,
        IReadOnlyList<AssetPath> assetUris, CancellationToken cancellationToken = default)
        => this.statusReader.ReadAsync(project, Publication.CookPublicationReadLease.Issued(publication), assetUris, cancellationToken);
}
