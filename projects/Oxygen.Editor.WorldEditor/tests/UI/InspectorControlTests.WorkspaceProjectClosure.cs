// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Security.Cryptography;
using AwesomeAssertions;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.World.Tests;

/// <summary>Qualifies project closure against queued and captured native material publication.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Closing a project cancels its cook, releases native readers and preserves the prior publication for reopening.</summary>
    /// <param name="captured">Whether native output has completed and preview capture is pending.</param>
    /// <returns>The project-lifetime integration check.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task ClosingProjectCancelsCookAndReopensPriorNativePublication(bool captured)
        => EnqueueAsync(() => this.CheckWorkspacePublicationAsync(scope: null, (scenario, token) => CheckProjectClosureAsync(scenario, captured, token)));

    private static Dictionary<string, string> ReadPublishedHashes(string projectRoot)
        => Directory.GetFiles(Path.Combine(projectRoot, ".cooked"), "*", SearchOption.AllDirectories)
            .ToDictionary(static path => path, static path => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))), StringComparer.OrdinalIgnoreCase);

    private static async Task CheckProjectClosureAsync(PublicationScenario scenario, bool captured, CancellationToken cancellationToken)
    {
        var fixture = scenario.Fixture;
        var services = scenario.Services;
        var project = services.Projects.ActiveProject!;
        _ = (await fixture.Commands.SaveSceneAsync(fixture.Context).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        var hashes = ReadPublishedHashes(fixture.ProjectRoot);
        var prior = services.Runs.Runs.Select(static run => run.OperationId).ToHashSet();
        var runId = fixture.Runtime.ContentStatus.RunId;
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var registration = services.Publication.RegisterPreview(project, async () =>
        {
            var preview = await fixture.CreateWorkspacePreviewAsync(services, scenario.Catalog, project).ConfigureAwait(false);
            _ = entered.TrySetResult();
            await release.Task.ConfigureAwait(false);
            return preview;
        });
        try
        {
            services.Runs.IsAutomaticCookingPaused = true;
            await SetPublicationMaterialColorAsync(scenario.Materials, scenario.Material.DocumentId, new(0, 1, 0, 1), cancellationToken).ConfigureAwait(true);
            _ = await WaitForProjectCookAsync(services, prior, completed: false, cancellationToken).ConfigureAwait(true);
            if (captured)
            {
                services.Runs.IsAutomaticCookingPaused = false;
                await entered.Task.WaitAsync(cancellationToken).ConfigureAwait(true);
            }

            services.Projects.Close();
            registration.Dispose();
            await fixture.Runtime.ShutdownAsync().AsTask().WaitAsync(cancellationToken).ConfigureAwait(true);
            _ = release.TrySetResult();
            var cancelled = await WaitForProjectCookAsync(services, prior, completed: true, cancellationToken).ConfigureAwait(true);
            _ = cancelled.State.Should().Be(CookRunState.Cancelled);
            _ = services.Projects.ActiveProject.Should().BeNull();
            await scenario.Picker.RefreshAsync(MaterialPickerFilter.Default, cancellationToken).ConfigureAwait(true);
            _ = ReadPublishedHashes(fixture.ProjectRoot).Should().BeEquivalentTo(hashes);
            _ = fixture.Runtime.State.Should().Be(EngineServiceState.NoEngine);
            using (CookOutputLease.AcquireWrite(fixture.ProjectRoot))
            {
            }

            await ReopenCancelledProjectAsync(scenario, project, runId, cancellationToken).ConfigureAwait(true);
        }
        finally
        {
            _ = release.TrySetResult();
        }
    }

    private static async Task<CookRunSnapshot> WaitForProjectCookAsync(CatalogWorkloadServices services, HashSet<Guid> prior, bool completed, CancellationToken cancellationToken)
    {
        while (true)
        {
            if (services.Runs.Runs.FirstOrDefault(run => !prior.Contains(run.OperationId) && run.IsCompleted == completed) is { } run)
            {
                return run;
            }

            await Task.Delay(20, cancellationToken).ConfigureAwait(true);
        }
    }

    private static async Task ReopenCancelledProjectAsync(PublicationScenario scenario, ProjectContext project, Guid previousRunId, CancellationToken cancellationToken)
    {
        var fixture = scenario.Fixture;
        var services = scenario.Services;
        services.Projects.Activate(project);
        using var registration = fixture.RegisterWorkspacePublication(services, scenario.Catalog);
        await fixture.InitializeAsync(cancellationToken).ConfigureAwait(true);
        var reader = await services.Publication.AcquireForMountAsync(project, cancellationToken).ConfigureAwait(true);
        var mounts = await services.Mounts.PrepareAsync(project, CookedContentMountService.FindProjectRoots(project), reader, cancellationToken).ConfigureAwait(true);
        await fixture.Runtime.RefreshProjectCookedRootsAsync(mounts.Roots, mounts).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(cancellationToken).ConfigureAwait(true);
        _ = fixture.Runtime.ContentStatus.RunId.Should().NotBe(previousRunId);
        foreach (var node in fixture.Source.RootNodes)
        {
            _ = await WaitForNodeAsync(fixture, node.Id, value => value.MaterialBaseColors.Length == 1 && value.MaterialBaseColors[0] == new Vector4(1, 0, 0, 1), cancellationToken).ConfigureAwait(true);
        }

        var result = await services.Pipeline.CookAssetAsync(scenario.Material.MaterialUri, cancellationToken).ConfigureAwait(true);
        _ = result.IsPublished.Should().BeTrue();
        _ = result.IsMounted.Should().BeTrue();
        foreach (var node in fixture.Source.RootNodes)
        {
            var updated = await fixture.ReadNodeAsync(node.Id, cancellationToken).ConfigureAwait(true);
            _ = updated.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(new Vector4(0, 1, 0, 1));
        }
    }
}
