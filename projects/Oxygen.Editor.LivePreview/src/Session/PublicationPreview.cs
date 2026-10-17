// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.LivePreview.Session;

/// <summary>Connects one cook publication to the engine's mounted content and the asset catalog.</summary>
/// <param name="project">The project whose output is being published.</param>
/// <param name="engine">The embedded engine.</param>
/// <param name="owner">The session's owner thread, where engine and catalog operations run.</param>
/// <param name="catalog">The catalog refreshed after publication.</param>
/// <param name="isCurrent">Checks that the session still owns the originating project.</param>
/// <param name="contentReady">Clears the session's content failure after the engine and the catalog recovered.</param>
internal sealed class PublicationPreview(
    ProjectContext project,
    IEngineService engine,
    OwnerThread owner,
    IAssetPublicationTarget catalog,
    Func<bool> isCurrent,
    Action contentReady) : ICookPublicationPreview
{
    /// <inheritdoc />
    public bool IsRuntimeAvailable { get; } = engine.State == EngineServiceState.Running;

    /// <inheritdoc />
    public Task PrepareReplacementAsync() => owner.InvokeAsync(async () =>
    {
        if (isCurrent() && this.IsRuntimeAvailable)
        {
            await engine.SuspendCookedContentAsync().ConfigureAwait(true);
        }
    });

    /// <inheritdoc />
    public async Task MountAsync(CookedContentMountSet mounts)
    {
        var pending = mounts;
        try
        {
            if (mounts.Publication.ProjectId != project.ProjectId)
            {
                throw new InvalidOperationException("Preview admission belongs to another project.");
            }

            await owner.InvokeAsync(async () =>
            {
                if (isCurrent() && this.IsRuntimeAvailable)
                {
                    var publication = mounts.Publication;
                    var bindings = publication.Roots.Zip(
                        publication.RootPaths,
                        static (root, path) => new RuntimeCookedRoot(path, root.Owner == CookPublicationRootOwner.Project ? root.Name : null)).ToArray();
                    pending = null;
                    await engine.RefreshProjectCookedRootsAsync(bindings, mounts, keepPaused: true).ConfigureAwait(true);
                }
            }).ConfigureAwait(false);
        }
        finally
        {
            pending?.Dispose();
        }
    }

    /// <inheritdoc />
    public Task ResumeAsync() => owner.InvokeAsync(async () =>
    {
        if (isCurrent() && this.IsRuntimeAvailable)
        {
            await engine.ResumeCookedContentAsync().ConfigureAwait(true);
        }
    });

    /// <inheritdoc />
    public Task CommittedAsync(CookPublicationReadLease publication) => owner.InvokeAsync(async () =>
    {
        if (isCurrent())
        {
            await catalog.UsePublicationAsync(publication, CancellationToken.None).ConfigureAwait(true);
            if (isCurrent() && this.IsRuntimeAvailable)
            {
                contentReady();
            }
        }
    });

    /// <inheritdoc />
    public ValueTask DisposeAsync() => ValueTask.CompletedTask;
}
