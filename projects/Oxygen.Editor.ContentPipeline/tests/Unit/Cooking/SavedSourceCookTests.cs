// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.SavedSourceScenario;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class SavedSourceCookTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Current-scene cooking cannot consume or mutate the active authoring scene.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task CurrentSceneCookKeepsSavedValuesWhenAuthoringAndSourceChangeLater()
    {
        using var workspace = new CookWorkspace();
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        workspace.Scene.Name = "Unsaved before cook";
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var generator = new CapturingSceneDescriptorGenerator([]);
        var api = new CapturingEngineContentPipelineApi(new(workspace.Root, Succeeded: true, []), SucceededInspection(workspace))
        {
            BeforeImport = (_, token) =>
            {
                entered.SetResult();
                return release.Task.WaitAsync(token);
            },
            SourceAnalysis = ControlledSourceAnalysis.AnalyzeAsync,
        };
        var service = CreateService(workspace, api, generator);
        var cook = service.CookCurrentSceneAsync(AssetPath.Parse("/Content/Scenes/Main.oscene.json"), this.TestContext.CancellationToken);
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
        using var workspace = new CookWorkspace();
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var release = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        var first = workspace.CookCoordinator.RunAsync((_, _) => release.Task, this.TestContext.CancellationToken);
        var generator = new CapturingSceneDescriptorGenerator([]);
        var service = CreateService(workspace, CreateSuccessfulApi(workspace, ControlledSourceAnalysis.AnalyzeAsync), generator);
        var cook = service.CookCurrentSceneAsync(AssetPath.Parse("/Content/Scenes/Main.oscene.json"), this.TestContext.CancellationToken);
        try
        {
            _ = generator.Scene.Should().BeNull();
            MarkVersion(workspace, "Latest saved");
            await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
            MarkVersion(workspace, "New unsaved edits");
        }
        finally
        {
            _ = release.TrySetResult(1);
        }

        _ = await first.ConfigureAwait(false);
        _ = (await cook.ConfigureAwait(false)).Status.Should().Be(OperationStatus.Succeeded);
        _ = VersionOf(generator.Scene!).Should().Be("Latest saved");
        _ = VersionOf(workspace.Scene).Should().Be("New unsaved edits");
    }

    // The scene's name is its file name, so a root node tells one saved version from another.
    private static void MarkVersion(CookWorkspace workspace, string version)
    {
        workspace.Scene.RootNodes.Clear();
        workspace.Scene.RootNodes.Add(new SceneNode(workspace.Scene, version));
    }

    private static string VersionOf(Scene scene) => scene.RootNodes.Single().Metadata.Name;
}
