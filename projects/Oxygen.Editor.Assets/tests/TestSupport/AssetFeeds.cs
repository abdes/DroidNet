// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Moq;
using Oxygen.Editor.Assets.Runtime;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Assets.TestSupport;

/// <summary>
/// Builds the asset feed a test reads. A test names the catalog, the project and the cook scope, and only the other
/// ports it varies; the rest report nothing. A new port of the feed changes this builder alone.
/// </summary>
internal static class AssetFeeds
{
    /// <summary>Creates an asset feed.</summary>
    /// <param name="catalog">The asset records.</param>
    /// <param name="session">The project session.</param>
    /// <param name="scopes">The project's cook scope.</param>
    /// <param name="cookStatus">The cook status; none of the assets has one when none is given.</param>
    /// <param name="documents">The unsaved documents; none when none is given.</param>
    /// <param name="cooks">The cook runs; none runs when none is given.</param>
    /// <param name="runtimeStatus">The preview's runtime status; unavailable when none is given.</param>
    /// <param name="relocation">The relocation service; one that is never called when none is given.</param>
    /// <param name="authoring">The asset authoring; one that is never called when none is given.</param>
    /// <returns>The feed, which the test disposes.</returns>
    public static AssetFeed Create(
        IAssetCatalogReader catalog,
        IProjectSession session,
        IProjectCookScopeProvider scopes,
        IAssetCookStatusReader? cookStatus = null,
        IUnsavedDocuments? documents = null,
        ICookRunService? cooks = null,
        IAssetRuntimeStatusSource? runtimeStatus = null,
        IAssetRelocationService? relocation = null,
        IAssetAuthoring? authoring = null)
        => new(
            catalog,
            session,
            scopes,
            cookStatus ?? new NoCookStatus(),
            documents ?? Mock.Of<IUnsavedDocuments>(port => port.Find(It.IsAny<IEnumerable<FileSystemPath>>()) == Array.Empty<UnsavedDocument>()),
            cooks ?? Mock.Of<ICookRunService>(runs => runs.Runs == Array.Empty<CookRunSnapshot>()),
            runtimeStatus ?? AssetStatusFixture.CreateUnavailableRuntimeStatus(),
            relocation ?? Mock.Of<IAssetRelocationService>(),
            authoring ?? Mock.Of<IAssetAuthoring>());

    // No asset has a cook status.
    private sealed class NoCookStatus : IAssetCookStatusReader
    {
        public event EventHandler? Changed { add { } remove { } }

        public Task<IReadOnlyList<AssetCookStatus>> ReadAsync(ProjectContext project, ContentPipeline.Publication.ICookPublicationReadLease publication, IReadOnlyList<AssetPath> assetUris, CancellationToken cancellationToken = default)
            => this.ReadAsync(project, assetUris, cancellationToken);

        public Task<IReadOnlyList<AssetCookStatus>> ReadAsync(ProjectContext project, IReadOnlyList<AssetPath> assetUris, CancellationToken cancellationToken = default)
            => Task.FromResult<IReadOnlyList<AssetCookStatus>>([]);
    }
}
