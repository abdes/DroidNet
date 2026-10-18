// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Cooking;

/// <summary>
/// A scene's script slots cook through the native import tool: each script compiles next to its source, and a script
/// sidecar attaches the slots and their values to the cooked scene.
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class ScriptCookTests
{
    private const string Move = """
        local Script = oxygen.script {
          params = {
            speed = oxygen.param.float { default = 1, min = 0 },
            target = oxygen.param.node { description = "Node to follow" },
          },
        }

        function Script:on_start()
        end

        return Script
        """;

    private static readonly AssetPath SceneUri = AssetPath.Parse("/Content/Scenes/Main.oscene.json");
    private static readonly AssetPath MoveScript = AssetPath.Parse("/Content/Player/move.oscript");

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Cooking a scene publishes its script next to the source and the scene with its slot, then reuses both.</summary>
    /// <returns>The asynchronous cook.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task CookCurrentScenePublishesItsScriptsAndSlots()
    {
        using var workspace = new CookWorkspace();
        var slot = AddScriptedNodes(workspace, out var target)
            .WithOverride("speed", ScriptValue.FromFloat(2.5f))
            .WithOverride("target", ScriptValue.FromNode(target.Id));
        await this.WriteSceneAsync(workspace, slot).ConfigureAwait(false);
        var service = CreateNativeService(workspace, out var compatibility);
        using var lifetime = compatibility;

        var result = await service.CookCurrentSceneAsync(SceneUri, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.Code + ": " + (issue.TechnicalMessage ?? issue.Message))));
        _ = File.Exists(workspace.CookedPath("Content/Player/move.oscript")).Should().BeTrue();
        _ = result.CookedAssets.Should().Contain(static asset => asset.SourceAssetUri == MoveScript);
        var again = await service.CookCurrentSceneAsync(SceneUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = again.IsUpToDate.Should().BeTrue(string.Join("; ", again.Diagnostics.Select(static issue => issue.Code + ": " + (issue.TechnicalMessage ?? issue.Message))));

        // Changing only a value recooks the scene; its sidecar finds the reused script in the published content.
        var player = workspace.Scene.RootNodes.Single(static node => string.Equals(node.Name, "Player", StringComparison.Ordinal));
        var scripting = player.Components.OfType<ScriptingComponent>().Single();
        scripting.Slots = [scripting.Slots[0].WithOverride("speed", ScriptValue.FromFloat(4f))];
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        var edited = await service.CookCurrentSceneAsync(SceneUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = edited.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, edited.Diagnostics.Select(static issue => issue.Code + ": " + (issue.TechnicalMessage ?? issue.Message))));
        _ = edited.ReusedAssets.Should().Contain(static asset => asset.SourceAssetUri == MoveScript);
        _ = edited.CookedAssets.Should().Contain(static asset => asset.SourceAssetUri == SceneUri);
    }

    /// <summary>A value the script does not declare fails the cook, pointing at the slot's parameter.</summary>
    /// <returns>The asynchronous cook.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task CookCurrentSceneFailsForAStaleValueAndPointsAtIt()
    {
        using var workspace = new CookWorkspace();
        var slot = AddScriptedNodes(workspace, out _).WithOverride("jump_height", ScriptValue.FromFloat(3f));
        await this.WriteSceneAsync(workspace, slot).ConfigureAwait(false);
        var service = CreateNativeService(workspace, out var compatibility);
        using var lifetime = compatibility;

        var result = await service.CookCurrentSceneAsync(SceneUri, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.IsPublished.Should().BeFalse();
        var stale = result.Diagnostics.Should().ContainSingle(static issue => issue.Code == "script.sidecar.param_undeclared").Subject;
        _ = stale.Message.Should().Contain("'jump_height'").And.Contain("'Player'");
        _ = stale.SuggestedAction!.Payload["PropertyPath"].Should().Be(ScriptSlotDiagnostics.PropertyPath(slot.Id, "jump_height"));
    }

    /// <summary>A script from a mounted cooked library reports the parameters its cooked descriptor records.</summary>
    /// <returns>The asynchronous cook and inspection.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ALibraryScriptReportsItsCookedDeclarations()
    {
        using var library = new CookWorkspace();
        using var consumer = new CookWorkspace();
        await this.WriteSceneAsync(library, AddScriptedNodes(library, out _)).ConfigureAwait(false);
        var producer = CreateNativeService(library, out var compatibility);
        using (compatibility)
        {
            _ = (await producer.CookCurrentSceneAsync(SceneUri, this.TestContext.CancellationToken).ConfigureAwait(false)).IsPublished.Should().BeTrue();
        }

        var root = await library.ExportRootAsync("Content", this.TestContext.CancellationToken).ConfigureAwait(false);
        var context = consumer.ProjectContext with { LocalFolderMounts = [new("Script library", FileSystemPath.From(root))] };
        consumer.Activate(context);
        consumer.WriteText("Content/Local/own.lua", Move);
        IScriptDeclarationProvider service = CreateNativeService(consumer, out var consumerCompatibility);
        using var lifetime = consumerCompatibility;

        var declarations = await service.ReadAsync(context, MoveScript, this.TestContext.CancellationToken).ConfigureAwait(false);
        var own = await service.ReadAsync(context, AssetPath.Parse("/Content/Local/own.oscript"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = declarations.Should().NotBeNull();
        _ = declarations!.Parameters.Should().Equal(
            new CookedScriptParameter("speed", CookedScriptParameterKind.Float, 1f, 0),
            new CookedScriptParameter("target", CookedScriptParameterKind.Node, null, 0));
        _ = own.Should().BeNull("a script with its source in the project is analyzed from the source");
    }

    private static ContentPipelineService CreateNativeService(CookWorkspace workspace, out EditorNativeCompatibilityService compatibility)
    {
        compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        return CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
    }

    private static ScriptSlot AddScriptedNodes(CookWorkspace workspace, out SceneNode target)
    {
        workspace.WriteText("Content/Player/move.lua", Move);
        target = new SceneNode(workspace.Scene) { Name = "Target" };
        workspace.Scene.RootNodes.Add(target);
        return new ScriptSlot { Script = MoveScript, ExecutionOrder = 3 };
    }

    private async Task WriteSceneAsync(CookWorkspace workspace, ScriptSlot slot)
    {
        var player = new SceneNode(workspace.Scene) { Name = "Player" };
        _ = player.AddComponent(new ScriptingComponent { Name = ScriptingComponent.DefaultName, Slots = [slot] });
        workspace.Scene.RootNodes.Add(player);
        workspace.Scene.SetEnvironment(new SceneEnvironmentData { PostProcess = new PostProcessEnvironmentData { ExposureMode = ExposureMode.Auto } });
        this.TestContext.CancellationToken.ThrowIfCancellationRequested();
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
    }
}
