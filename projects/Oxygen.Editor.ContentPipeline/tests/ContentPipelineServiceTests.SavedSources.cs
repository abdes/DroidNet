// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies saved-source ownership at the production cook entry points.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>Current-scene cooking cannot consume or mutate the active authoring scene.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CurrentSceneCookKeepsSavedValuesWhenAuthoringAndSourceChangeLater()
    {
        using var workspace = new TempWorkspace();
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        workspace.Scene.Name = "Unsaved before cook";
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var generator = new CapturingSceneDescriptorGenerator(workspace, [])
        {
            BeforeGenerate = token =>
            {
                entered.SetResult();
                return release.Task.WaitAsync(token);
            },
        };
        var service = CreateService(workspace, generator, CreateSuccessfulApi(workspace));
        var cook = service.CookCurrentSceneAsync(new("asset:///Content/Scenes/Main.oscene.json"), this.TestContext.CancellationToken);
        await entered.Task.WaitAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        try
        {
            workspace.Scene.Name = "Saved after capture";
            await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
            workspace.Scene.Name = "Still unsaved";
            _ = generator.Scene.Should().NotBeSameAs(workspace.Scene);
            _ = generator.Scene!.Name.Should().Be("Main");
        }
        finally
        {
            _ = release.TrySetResult();
        }

        _ = (await cook.ConfigureAwait(false)).Status.Should().Be(OperationStatus.Succeeded);
        _ = workspace.Scene.Name.Should().Be("Still unsaved");
    }

    /// <summary>A queued request reads the saved scene only after it owns the project writer.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task QueuedSceneCookReadsLatestSavedSourceAfterAcquiringWriter()
    {
        using var workspace = new TempWorkspace();
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var release = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        var first = workspace.CookCoordinator.RunAsync((_, _) => release.Task, this.TestContext.CancellationToken);
        var generator = new CapturingSceneDescriptorGenerator(workspace, []);
        var service = CreateService(workspace, generator, CreateSuccessfulApi(workspace));
        var cook = service.CookCurrentSceneAsync(new("asset:///Content/Scenes/Main.oscene.json"), this.TestContext.CancellationToken);
        try
        {
            _ = generator.Scene.Should().BeNull();
            workspace.Scene.Name = "Latest saved";
            await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
            workspace.Scene.Name = "New unsaved edits";
        }
        finally
        {
            _ = release.TrySetResult(1);
        }

        _ = await first.ConfigureAwait(false);
        _ = (await cook.ConfigureAwait(false)).Status.Should().Be(OperationStatus.Succeeded);
        _ = generator.Scene!.Name.Should().Be("Latest saved");
        _ = workspace.Scene.Name.Should().Be("New unsaved edits");
    }

    /// <summary>Changed saved files require acknowledgement even when the document is not dirty.</summary>
    /// <param name="material">Whether to exercise material descriptor preparation.</param>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task UnacknowledgedExternalChangeFailsBeforeNativeImport(bool material)
    {
        using var workspace = new TempWorkspace();
        var relative = material ? "Content/Materials/Red.omat.json" : "Content/Scenes/Main.oscene.json";
        if (material)
        {
            workspace.WriteMaterial(relative, "Red");
        }
        else
        {
            await workspace.WriteSceneAsync(relative).ConfigureAwait(false);
        }

        var path = Path.Combine(workspace.Root, relative);
        var state = SavedState(path);
        using var registration = workspace.Documents.Register(path, _ => Task.FromResult<CookDocumentReadLease?>(new(state, static () => { })));
        await File.AppendAllTextAsync(path, " ", this.TestContext.CancellationToken).ConfigureAwait(false);
        var api = CreateSuccessfulApi(workspace);
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, []), api);
        var result = material
            ? await service.CookAssetAsync(new("asset:///" + relative), this.TestContext.CancellationToken).ConfigureAwait(false)
            : await service.CookCurrentSceneAsync(new("asset:///" + relative), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().Contain(diagnostic => diagnostic.Message.Contains("Reload", StringComparison.Ordinal));

        _ = api.ImportedManifest.Should().BeNull();
        _ = workspace.CookCoordinator.Runs.Single().State.Should().Be(CookRunState.Failed);
    }

    /// <summary>A document becoming dirty after the initial check is rechecked under the actual source read lease.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task SceneBecomingDirtyBeforeReadWaitsForExplicitSaveAndResume()
    {
        using var workspace = new TempWorkspace();
        const string relative = "Content/Scenes/Main.oscene.json";
        await workspace.WriteSceneAsync(relative).ConfigureAwait(false);
        var path = Path.Combine(workspace.Root, relative);
        var state = SavedState(path);
        var acquireCount = 0;
        using var registration = workspace.Documents.Register(path, _ =>
        {
            if (++acquireCount == 2)
            {
                state = state with { IsDirty = true, Revision = 2 };
            }

            return Task.FromResult<CookDocumentReadLease?>(new(state, static () => { }));
        });
        var blocked = new TaskCompletionSource<Guid>(TaskCreationOptions.RunContinuationsAsynchronously);
        workspace.CookCoordinator.RunChanged += (_, args) =>
        {
            if (args.Run.State == CookRunState.NeedsSave)
            {
                _ = blocked.TrySetResult(args.Run.OperationId);
            }
        };
        var generator = new CapturingSceneDescriptorGenerator(workspace, []);
        var service = CreateService(workspace, generator, CreateSuccessfulApi(workspace));
        var cook = service.CookCurrentSceneAsync(new("asset:///" + relative), this.TestContext.CancellationToken);
        var runId = await blocked.Task.WaitAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = generator.Scene.Should().BeNull();
        workspace.Scene.Name = "Explicitly saved";
        await workspace.WriteSceneAsync(relative).ConfigureAwait(false);
        state = SavedState(path) with { DocumentId = state.DocumentId, Revision = 2, SavedRevision = 2 };
        _ = workspace.CookCoordinator.ResumeAfterSave(runId).Should().BeTrue();
        _ = (await cook.ConfigureAwait(false)).Status.Should().Be(OperationStatus.Succeeded);
        _ = generator.Scene!.Name.Should().Be("Explicitly saved");
    }

    private static CookDocumentState SavedState(string path)
        => new(Guid.NewGuid(), path, Path.GetFileName(path), 1, 1, IsDirty: false, Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))));

    private static CapturingEngineContentPipelineApi CreateSuccessfulApi(TempWorkspace workspace)
        => new(new(Path.Combine(workspace.Root, ".cooked", "Content"), Succeeded: true, []), SucceededInspection(workspace));
}
