// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

/// <summary>
/// A scene's script slots cook as one script job per script, compiled next to its source, and one script sidecar job
/// that attaches the slots to the cooked scene.
/// </summary>
[TestClass]
internal sealed partial class ScriptCookingTests : IDisposable
{
    private static readonly AssetPath MoveScript = AssetPath.Parse("/Content/Player/move.oscript");
    private readonly string root = Path.Combine(Path.GetTempPath(), "oxygen-script-cooking-tests", Guid.NewGuid().ToString("N"));
    private readonly ProjectContext project;

    public ScriptCookingTests()
    {
        Directory.CreateDirectory(Path.Combine(this.root, "Content", "Player"));
        Directory.CreateDirectory(Path.Combine(this.root, "Content", "Scenes"));
        File.WriteAllText(Path.Combine(this.root, "Content", "Player", "move.lua"), "return {}");
        File.WriteAllText(Path.Combine(this.root, "Content", "Scenes", "Main.oscene.json"), "{}");
        this.project = TestProject.Create(new RealFileSystem(), this.root).Context;
    }

    public TestContext TestContext { get; set; } = null!;

    public void Dispose()
    {
        if (Directory.Exists(this.root))
        {
            Directory.Delete(this.root, recursive: true);
        }
    }

    [TestMethod]
    public void ResolveScriptFindsItsSourceAndKeepsItsFolder()
    {
        var input = CookInputResolver.Resolve(this.project, MoveScript, ContentCookInputRole.Dependency);

        _ = input.Kind.Should().Be(ContentCookAssetKind.Script);
        _ = input.AssetUri.Should().Be(MoveScript);
        _ = input.SourceRelativePath.Should().Be("Content/Player/move.lua");
    }

    [TestMethod]
    [DataRow("/Content/Player/move.oscript", "Content/Player/move.lua")]
    [DataRow("/Content/Player/move.lua", "Content/Player/move.lua")]
    [DataRow("/Content/Player/move.luau", "Content/Player/move.luau")]
    public void ResolveDeletedScriptGivesItsMissingSource(string requested, string source)
    {
        File.Delete(Path.Combine(this.root, "Content", "Player", "move.lua"));

        var input = CookInputResolver.Resolve(this.project, AssetPath.Parse(requested), ContentCookInputRole.Primary);

        // The cook reports the source missing and removes what the script published, as for any deleted source.
        _ = input.Kind.Should().Be(ContentCookAssetKind.Script);
        _ = input.AssetUri.Should().Be(MoveScript);
        _ = input.SourceRelativePath.Should().Be(source);
        _ = File.Exists(input.SourceAbsolutePath.FullPath).Should().BeFalse();
    }

    [TestMethod]
    public void BuildJobCompilesTheScriptAndRecordsItsSourceUnderTheMount()
    {
        var input = CookInputResolver.Resolve(this.project, MoveScript, ContentCookInputRole.Dependency);

        var job = new ContentImportManifestBuilder().BuildJob(input, []);

        _ = job.Type.Should().Be("script");
        _ = job.Id.Should().Be("script:Content/Player/move.lua");
        _ = job.Compile.Should().BeTrue();
        _ = job.CompileMode.Should().Be("debug");
        _ = job.ScriptStorage.Should().Be("external");
        _ = job.SourceRoot.Should().Be(Path.Combine(this.root, "Content").Replace('\\', '/'));
        _ = job.VirtualPath.Should().Be(MoveScript.VirtualPath, "the cooker places a script at its identity");
        _ = job.Layout.Should().Be(new ContentImportLayout("/Content"));
    }

    [TestMethod]
    public async Task GenerateAsyncProjectsSlotsIntoSidecarBindings()
    {
        var scene = CreateScene(out var target, out var player);
        var slot = new ScriptSlot { Script = MoveScript, ExecutionOrder = 2 }
            .WithOverride("speed", ScriptValue.FromFloat(2.5f))
            .WithOverride("follow", ScriptValue.FromNode(target.Id))
            .WithOverride("jump", ScriptValue.FromAsset(AssetPath.Parse("/Content/Input/Jump.oiact")))
            .WithOverride("offset", ScriptValue.FromVector(new System.Numerics.Vector2(1, 2)));
        var empty = new ScriptSlot();
        _ = player.AddComponent(new ScriptingComponent { Name = ScriptingComponent.DefaultName, Slots = [slot, empty] });

        var result = await this.GenerateAsync(scene).ConfigureAwait(false);

        var binding = result.ScriptBindings.Should().ContainSingle().Subject;
        _ = binding.NodeIndex.Should().Be(1u);
        _ = binding.SlotId.Should().Be(slot.Id.ToString("D"));
        _ = binding.ScriptVirtualPath.Should().Be("/Content/Player/move.oscript");
        _ = binding.ExecutionOrder.Should().Be(2);
        _ = binding.Params.Select(static param => (param.Key, param.Type)).Should().Equal(
            ("follow", "node"), ("jump", "asset"), ("offset", "vec2"), ("speed", "float"));
        _ = binding.Params.Single(static param => string.Equals(param.Key, "follow", StringComparison.Ordinal)).Value.Should().Be(new ScriptSidecarNodeRef(0u));
        _ = binding.Params.Single(static param => string.Equals(param.Key, "jump", StringComparison.Ordinal)).Value.Should().Be("/Content/Input/Jump.oiact");
        _ = result.ScriptReferences.Should().BeEquivalentTo([MoveScript, AssetPath.Parse("/Content/Input/Jump.oiact")]);
        var warning = result.Diagnostics.Should().ContainSingle().Subject;
        _ = warning.Severity.Should().Be(DiagnosticSeverity.Warning);
        _ = warning.Code.Should().Be(ScriptDiagnosticCodes.SlotWithoutScript);
        _ = warning.SuggestedAction!.Payload["PropertyPath"].Should().Be(new ScriptFieldAddress(empty.Id).ToString());
        _ = warning.SuggestedAction.Payload["NodeId"].Should().Be(player.Id.ToString());
    }

    [TestMethod]
    public async Task GenerateAsyncProjectsANodeAnInstanceBringsInAsItsRootAndPath()
    {
        var scene = CreateScene(out var target, out var player);
        var instanced = NodeId.New();
        var slot = new ScriptSlot { Script = MoveScript }
            .WithOverride("follow", ScriptValue.FromNode(new NodeReference(target.Id, NodePath.Of(instanced))));
        _ = player.AddComponent(new ScriptingComponent { Name = ScriptingComponent.DefaultName, Slots = [slot] });

        var result = await this.GenerateAsync(scene).ConfigureAwait(false);

        var follow = result.ScriptBindings.Should().ContainSingle().Subject.Params.Should().ContainSingle().Subject;
        var node = follow.Value.Should().BeOfType<ScriptSidecarNodeRef>().Subject;
        _ = node.Node.Should().Be(0u);
        _ = node.Path.Should().Equal(instanced.ToString());
    }

    [TestMethod]
    public async Task GenerateAsyncProjectsTheParameterChangesOfAnInstance()
    {
        var scene = CreateScene(out var target, out var player);
        var guard = NodeId.New();
        var slot = Guid.NewGuid();
        var changes = InstancedNodeChanges.None
            .WithParameter(slot, "speed", ScriptValue.FromFloat(4f))
            .WithParameter(slot, "follow", ScriptValue.FromNode(player.Id));
        _ = target.AddComponent(new SceneInstanceComponent
        {
            Name = "Scene Instance",
            Source = AssetPath.Parse("/Content/Prefabs/Watchtower.oscene"),
            Changes = InstanceChanges.None.With(NodePath.Of(guard), changes),
        });

        var result = await this.GenerateAsync(scene).ConfigureAwait(false);

        _ = result.ScriptBindings.Should().BeEmpty();
        _ = result.HasScriptSidecar.Should().BeTrue();
        var row = result.ScriptInstanceBindings.Should().ContainSingle().Subject;
        _ = row.NodeIndex.Should().Be(0u);
        _ = row.NodePath.Should().Equal(guard.ToString());
        _ = row.SlotId.Should().Be(slot.ToString("D"));
        _ = row.Params.Select(static param => (param.Key, param.Type)).Should().Equal(("follow", "node"), ("speed", "float"));
        _ = row.Params[0].Value.Should().Be(new ScriptSidecarNodeRef(1u));
    }

    [TestMethod]
    public void BuildSceneManifestsAddsASidecarForInstanceParameterChangesAlone()
    {
        var row = new ScriptSidecarInstanceBinding(0, [Guid.NewGuid().ToString("D")], Guid.NewGuid().ToString("D"), [new ScriptSidecarParam("speed", "float", 4f)]);
        var descriptor = new SceneDescriptorGenerationResult(
            AssetPath.Parse("/Content/Scenes/Main.oscene"),
            Path.Combine(this.root, ".pipeline", "Scenes", "Main.oscene.json"),
            "/Content/Scenes/Main.oscene",
            Dependencies: [],
            Diagnostics: [])
        {
            ScriptInstanceBindings = [row],
        };

        var manifest = new ContentImportManifestBuilder().BuildSceneManifests(this.CreateScope(), [descriptor]);

        var sidecar = manifest.Jobs.Should().ContainSingle(static job => job.Type == "script-sidecar").Subject;
        _ = sidecar.InstanceBindings.Should().Equal(row);
        using var json = JsonDocument.Parse(JsonSerializer.Serialize(sidecar));
        _ = json.RootElement.GetProperty("bindings").GetArrayLength().Should().Be(0, "the cooker takes the instance rows beside inline bindings");
        var written = json.RootElement.GetProperty("instance_bindings")[0];
        _ = written.GetProperty("node_path")[0].GetString().Should().Be(row.NodePath[0]);
        _ = written.GetProperty("slot_id").GetString().Should().Be(row.SlotId);
        _ = new ContentImportManifestValidator().Validate(Guid.NewGuid(), manifest).Should().BeEmpty();
    }

    [TestMethod]
    public async Task GenerateAsyncFailsForAMissingNodeOrScript()
    {
        var scene = CreateScene(out _, out var player);
        var lost = new ScriptSlot { Script = MoveScript }.WithOverride("follow", ScriptValue.FromNode(NodeId.New()));
        var gone = new ScriptSlot { Script = AssetPath.Parse("/Content/Player/gone.oscript") };
        _ = player.AddComponent(new ScriptingComponent { Name = ScriptingComponent.DefaultName, Slots = [lost, gone] });

        var result = await this.GenerateAsync(scene).ConfigureAwait(false);

        _ = result.Diagnostics.Select(static issue => issue.Code).Should().BeEquivalentTo(
            [ScriptDiagnosticCodes.SlotNodeMissing, ScriptDiagnosticCodes.SlotScriptMissing]);
        _ = result.Diagnostics.Should().AllSatisfy(static issue => issue.Severity.Should().Be(DiagnosticSeverity.Error));
        _ = result.Diagnostics.Single(static issue => string.Equals(issue.Code, ScriptDiagnosticCodes.SlotNodeMissing, StringComparison.Ordinal)).SuggestedAction!.Payload["PropertyPath"]
            .Should().Be(new ScriptFieldAddress(lost.Id, "follow").ToString());
        _ = File.Exists(result.DescriptorPath).Should().BeFalse("a scene with slot errors is not projected");
    }

    [TestMethod]
    public void BuildSceneManifestsAddsASidecarAfterTheSceneAndItsScripts()
    {
        var context = this.project;
        var script = CookInputResolver.Resolve(context, MoveScript, ContentCookInputRole.Dependency);
        var scope = this.CreateScope() with { Inputs = [.. this.CreateScope().Inputs, script] };
        var binding = new ScriptSidecarBinding(1, Guid.NewGuid().ToString("D"), "/Content/Player/move.oscript", 0, [new ScriptSidecarParam("speed", "float", 2.5f)]);
        var descriptor = new SceneDescriptorGenerationResult(
            AssetPath.Parse("/Content/Scenes/Main.oscene"),
            Path.Combine(this.root, ".pipeline", "Scenes", "Main.oscene.json"),
            "/Content/Scenes/Main.oscene",
            Dependencies: [],
            Diagnostics: [])
        {
            ScriptBindings = [binding],
            ScriptReferences = [MoveScript],
        };

        var manifest = new ContentImportManifestBuilder().BuildSceneManifests(scope, [descriptor]);

        var sidecar = manifest.Jobs.Should().ContainSingle(static job => job.Type == "script-sidecar").Subject;
        var scene = manifest.Jobs.Single(static job => string.Equals(job.Type, "scene-descriptor", StringComparison.Ordinal));
        _ = sidecar.Source.Should().BeNull();
        _ = sidecar.OwnerSource.Should().Be(scene.Source);
        _ = sidecar.DependsOn.Should().Equal(scene.Id, "script:Content/Player/move.lua");
        _ = sidecar.TargetSceneVirtualPath.Should().Be("/Content/Scenes/Main.oscene");
        _ = sidecar.Bindings.Should().Equal(binding);
        using var json = JsonDocument.Parse(JsonSerializer.Serialize(sidecar));
        _ = json.RootElement.TryGetProperty("source", out _).Should().BeFalse("the cooker takes either a source or inline bindings");
        var row = json.RootElement.GetProperty("bindings")[0];
        _ = row.GetProperty("node_index").GetUInt32().Should().Be(1u);
        _ = row.GetProperty("params")[0].GetProperty("value").GetSingle().Should().Be(2.5f);
        _ = new ContentImportManifestValidator().Validate(Guid.NewGuid(), manifest).Should().BeEmpty();
    }

    [TestMethod]
    public void SidecarDiagnosticsPointAtTheirSlotAndParameter()
    {
        var site = new ScriptBindingSite(NodeId.New(), "Player", Guid.NewGuid(), SlotNumber: 1, ["follow", "speed"]);
        var descriptor = new SceneDescriptorGenerationResult(
            AssetPath.Parse("/Content/Scenes/Main.oscene"),
            Path.Combine(this.root, ".pipeline", "Scenes", "Main.oscene.json"),
            "/Content/Scenes/Main.oscene",
            Dependencies: [],
            Diagnostics: [])
        {
            SceneId = SceneId.New(),
            SceneName = "Main",
            ScriptBindings = [new ScriptSidecarBinding(1, site.SlotId.ToString("D"), "/Content/Player/move.oscript", 0, [])],
            ScriptBindingSites = [site],
        };
        var issue = new DiagnosticRecord
        {
            OperationId = Guid.NewGuid(),
            Domain = FailureDomain.AssetImport,
            Severity = DiagnosticSeverity.Error,
            Code = "script.sidecar.param_undeclared",
            Message = "Script '/Content/Player/move.oscript' declares no parameter 'speed'",
            TechnicalMessage = "bindings[0].params[1].key",
            AffectedPath = descriptor.DescriptorPath,
        };

        var mapped = ContentPipelineService.MapScriptSidecarDiagnostic(this.CreateScope(), [descriptor], issue);

        _ = mapped.Code.Should().Be(issue.Code);
        _ = mapped.Message.Should().StartWith("Parameter 'speed' of slot 1 of 'Player': ");
        _ = mapped.AffectedEntity!.NodeId.Should().Be(site.NodeId);
        _ = mapped.SuggestedAction!.Payload["PropertyPath"].Should().Be(new ScriptFieldAddress(site.SlotId, "speed").ToString());
    }

    [TestMethod]
    public void InstanceSidecarDiagnosticsPointAtTheInstanceRootAndTheInstancedSlot()
    {
        var path = NodePath.Of(NodeId.New());
        var site = new ScriptBindingSite(NodeId.New(), "Tower", Guid.NewGuid(), SlotNumber: 0, ["speed"]) { Path = path, InstancedNames = "Guard" };
        var descriptor = new SceneDescriptorGenerationResult(
            AssetPath.Parse("/Content/Scenes/Main.oscene"),
            Path.Combine(this.root, ".pipeline", "Scenes", "Main.oscene.json"),
            "/Content/Scenes/Main.oscene",
            Dependencies: [],
            Diagnostics: [])
        {
            SceneId = SceneId.New(),
            SceneName = "Main",
            ScriptInstanceBindings = [new ScriptSidecarInstanceBinding(0, [path.ToString()], site.SlotId.ToString("D"), [])],
            ScriptInstanceBindingSites = [site],
        };
        var issue = new DiagnosticRecord
        {
            OperationId = Guid.NewGuid(),
            Domain = FailureDomain.AssetImport,
            Severity = DiagnosticSeverity.Error,
            Code = "script.sidecar.param_undeclared",
            Message = "The slot's script declares no parameter 'speed'",
            TechnicalMessage = "instance_bindings[0].params[0].key",
            AffectedPath = descriptor.DescriptorPath,
        };

        var mapped = ContentPipelineService.MapScriptSidecarDiagnostic(this.CreateScope(), [descriptor], issue);

        _ = mapped.Message.Should().StartWith("Parameter 'speed' of a script slot of 'Guard' in the instance under 'Tower': ");
        _ = mapped.AffectedEntity!.NodeId.Should().Be(site.NodeId);
        _ = mapped.AffectedEntity.ComponentName.Should().Be("Scene Instance");
        _ = mapped.SuggestedAction!.Payload["NodeReference"].Should().Be(new NodeReference(site.NodeId, path).ToString());
        _ = mapped.SuggestedAction.Payload["PropertyPath"].Should().Be(new ScriptFieldAddress(site.SlotId, "speed").ToString());
    }

    [TestMethod]
    public void AnUnresolvedInstanceParameterChange_IsAKeptChangeOfItsInstanceRoot()
    {
        var path = NodePath.Of(NodeId.New());
        var site = new ScriptBindingSite(NodeId.New(), "Tower", Guid.NewGuid(), SlotNumber: 0, ["speed"]) { Path = path, InstancedNames = "Guard" };
        var descriptor = new SceneDescriptorGenerationResult(
            AssetPath.Parse("/Content/Scenes/Main.oscene"),
            Path.Combine(this.root, ".pipeline", "Scenes", "Main.oscene.json"),
            "/Content/Scenes/Main.oscene",
            Dependencies: [],
            Diagnostics: [])
        {
            SceneId = SceneId.New(),
            SceneName = "Main",
            ScriptInstanceBindings = [new ScriptSidecarInstanceBinding(0, [path.ToString()], site.SlotId.ToString("D"), [])],
            ScriptInstanceBindingSites = [site],
            Nodes = [new DescriptorNodeSite(site.NodeId, "Tower")],
        };
        var issue = new DiagnosticRecord
        {
            OperationId = Guid.NewGuid(),
            Domain = FailureDomain.AssetImport,
            Severity = DiagnosticSeverity.Warning,
            Code = KeptChangeDiagnostics.ScriptInstanceOverrideUnresolved,
            Message = "The change names no node of the instanced scene",
            TechnicalMessage = "instance_bindings[0]",
            AffectedPath = descriptor.DescriptorPath,
        };

        var mapped = ContentPipelineService.MapKeptChangeDiagnostic(this.CreateScope(), [descriptor], issue);

        _ = mapped.Message.Should().StartWith("Kept change of 'Guard' in the instance under 'Tower': ");
        _ = mapped.AffectedEntity!.NodeId.Should().Be(site.NodeId);
        _ = mapped.SuggestedAction!.Payload["PropertyPath"].Should().Be(KeptChangeDiagnostics.PropertyPath);
    }

    private static Scene CreateScene(out SceneNode target, out SceneNode player)
    {
        var scene = new Scene { Name = "Main", Id = SceneId.New() };
        scene.SetEnvironment(new SceneEnvironmentData { PostProcess = new PostProcessEnvironmentData { ExposureMode = ExposureMode.Auto } });
        target = new SceneNode(scene, "Target");
        player = new SceneNode(scene, "Player");
        scene.RootNodes.Add(target);
        scene.RootNodes.Add(player);
        return scene;
    }

    private Task<SceneDescriptorGenerationResult> GenerateAsync(Scene scene)
        => new SceneDescriptorGenerator()
            .GenerateAsync(scene, this.CreateScope(), this.TestContext.CancellationToken);

    private ContentCookScope CreateScope()
    {
        var context = this.project;
        var key = Guid.CreateVersion7();
        return new ContentCookScope(
            context,
            new ProjectCookScope(context.ProjectId, FileSystemPath.From(this.root), Path.Combine(this.root, ".cooked")),
            [
                new ContentCookInput(
                    AssetPath.Parse("/Content/Scenes/Main.oscene"),
                    ContentCookAssetKind.Scene,
                    "Content",
                    "Content/Scenes/Main.oscene.json",
                    FileSystemPath.From(Path.Combine(this.root, "Content", "Scenes", "Main.oscene.json")),
                    ContentCookInputRole.Primary),
            ],
            CookTargetKind.CurrentScene)
        { Output = new CookStagingRoot("Content", key, CookPublicationPaths.Generation(this.root, key)) };
    }
}
