// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Hosting.WinUI;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.World.Workspace;

/// <summary>Connects publication to the active workspace's native bindings and asset catalog.</summary>
/// <param name="project">The project whose output is being published.</param>
/// <param name="engine">The workspace runtime.</param>
/// <param name="hosting">The dispatcher owning workspace and runtime operations.</param>
/// <param name="mountService">The ordered, validated mount owner.</param>
/// <param name="catalog">The catalog refreshed after publication.</param>
/// <param name="messenger">Asset-change notifications for workspace consumers.</param>
/// <param name="isCurrent">Checks that this workspace still owns the originating project.</param>
internal sealed class WorkspacePublicationPreview(
    ProjectContext project,
    IEngineService engine,
    HostingContext hosting,
    CookedContentMountService mountService,
    IProjectAssetCatalog catalog,
    IMessenger? messenger,
    Func<bool> isCurrent) : ICookPublicationPreview
{
    /// <inheritdoc />
    public bool IsRuntimeAvailable { get; } = engine.State == EngineServiceState.Running;

    /// <inheritdoc />
    public Task PrepareReplacementAsync() => hosting.Dispatcher.DispatchAsync(async () =>
    {
        if (isCurrent() && this.IsRuntimeAvailable)
        {
            await engine.SuspendCookedContentAsync().ConfigureAwait(true);
        }
    });

    /// <inheritdoc />
    public Task MountAsync(IReadOnlyList<string> roots, CookOutputWriteLease? writer) => hosting.Dispatcher.DispatchAsync(async () =>
    {
        if (isCurrent() && this.IsRuntimeAvailable)
        {
            var reader = writer?.CreateReader() ?? CookOutputLease.AcquireRead(project.ProjectRoot);
            var mounts = await mountService.PrepareAsync(project, roots, reader, CancellationToken.None).ConfigureAwait(true);
            await engine.RefreshProjectCookedRootsAsync(mounts.Roots, mounts, keepPaused: true).ConfigureAwait(true);
        }
    });

    /// <inheritdoc />
    public Task ResumeAsync() => hosting.Dispatcher.DispatchAsync(async () =>
    {
        if (isCurrent())
        {
            await catalog.RefreshAsync(CancellationToken.None).ConfigureAwait(true);
            _ = messenger?.Send(new AssetsChangedMessage());
            if (this.IsRuntimeAvailable)
            {
                await engine.ResumeCookedContentAsync().ConfigureAwait(true);
            }
        }
    });

    /// <inheritdoc />
    public ValueTask DisposeAsync() => ValueTask.CompletedTask;
}
