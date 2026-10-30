// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using DroidNet.Documents;
using DroidNet.TimeMachine;
using Moq;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Relocation;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class AssetRelocationTests
{
    private static readonly Guid MainSceneId = Guid.Parse("6a1f2c0e-6c1e-4a59-9a77-1f6a3e4b2d10");

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Renaming a material rewrites scene surface overrides and geometry material references.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingMaterialRewritesEveryReferenceInItsForm()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var plan = await workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Blue.omat.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.Exists("Content/Materials/Red.omat.json").Should().BeFalse();
        _ = workspace.Exists("Content/Materials/Blue.omat.json").Should().BeTrue();
        _ = plan.Referrers.Should().BeEquivalentTo([AssetPath.Parse("/Content/Geometry/Box.ogeo.json"), AssetPath.Parse("/Content/Scenes/Main.oscene.json")]);
        var scene = await workspace.ReadSceneAsync("Content/Scenes/Main.oscene.json", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = MaterialsOf(scene).Should().NotContain(AssetPath.Parse("/Content/Materials/Red.omat")).And.Contain(AssetPath.Parse("/Content/Materials/Blue.omat"));
        _ = MaterialOf(GeometryOf(scene, "Box")).Should().Be(AssetPath.Parse("/Content/Materials/Blue.omat"));
        _ = workspace.ReadJson("Content/Geometry/Box.ogeo.json")["lods"]![0]!["submeshes"]![0]!["material_ref"]!.GetValue<string>().Should().Be(Spell("/Content/Geometry/Box.ogeo", "/Content/Materials/Blue.omat"));
    }

    /// <summary>
    /// A relocation rewrites the fields a format names as references, and leaves text that only looks like a path:
    /// a node's name, a script's string parameter, or a field of a format that is not a reference.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RelocationRewritesReferenceFieldsAndLeavesText()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var named = new Scene { Name = "Named", Id = SceneId.FromValue(Guid.Parse("0b0e6b8c-3c5a-4a0e-9a8f-1b1f2a3c4d5e")) };
        var namedNode = new SceneNode(named, "asset:///Content/Materials/Red.omat") { Id = new NodeId(Guid.Parse("3d9a2b1c-0000-4000-8000-000000000003")) };
        _ = namedNode.AddComponent(new ScriptingComponent
        {
            Name = ScriptingComponent.DefaultName,
            Slots =
            [
                new ScriptSlot { Id = Guid.Parse("0f8fad5b-d9cb-469f-a165-70867728950e"), Script = AssetPath.Parse("/Content/Scripts/paint.oscript") }
                    .WithOverride("label", ScriptValue.FromString("asset:///Content/Materials/Red.omat"))
                    .WithOverride("paint", ScriptValue.FromAsset(AssetPath.Parse("/Content/Materials/Red.omat"))),
            ],
        });
        named.RootNodes.Add(namedNode);

        // The collision shape descriptor has no writer in the kit: a format whose name field is the subject, kept as text.
        const string box = """
            { "name": "/Content/Materials/Red.omat", "virtual_path": "/Content/Shapes/Box.ocshape", "shape_type": "box", "material_ref": "/Content/Materials/Red.opmat" }
            """;
        _ = await workspace.WriteSceneAsync("Content/Scenes/Named.oscene.json", named, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Write("Content/Shapes/Box.ocshape.json", box);

        var plan = await workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Blue.omat.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = plan.Referrers.Should().Contain(AssetPath.Parse("/Content/Scenes/Named.oscene.json")).And.NotContain(AssetPath.Parse("/Content/Shapes/Box.ocshape.json"));
        var written = (await workspace.ReadSceneAsync("Content/Scenes/Named.oscene.json", this.TestContext.CancellationToken).ConfigureAwait(false)).RootNodes.Single();
        _ = written.Metadata.Name.Should().Be("asset:///Content/Materials/Red.omat", "a node's name is text");
        var overrides = written.Components.OfType<ScriptingComponent>().Single().Slots.Single().Overrides;
        _ = overrides["label"].StringValue.Should().Be("asset:///Content/Materials/Red.omat", "a string parameter is text");
        _ = overrides["paint"].Asset.Should().Be(AssetPath.Parse("/Content/Materials/Blue.omat"));
        _ = workspace.ReadJson("Content/Shapes/Box.ocshape.json")["name"]!.GetValue<string>().Should().Be("/Content/Materials/Red.omat");
    }

    /// <summary>A relocation undoes and redoes through the history it was recorded in.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RelocationUndoesAndRedoesThroughItsHistory()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var history = new HistoryKeeper(this);
        _ = await workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Blue.omat.json"), "Rename", history, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = history.UndoStack.Should().ContainSingle().Which.Key.Should().Be("Rename");

        await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Exists("Content/Materials/Red.omat.json").Should().BeTrue();
        _ = MaterialsOf(await workspace.ReadSceneAsync("Content/Scenes/Main.oscene.json", this.TestContext.CancellationToken).ConfigureAwait(false))
            .Should().Contain(AssetPath.Parse("/Content/Materials/Red.omat")).And.NotContain(AssetPath.Parse("/Content/Materials/Blue.omat"));
        _ = history.CanUndo.Should().BeFalse();

        await history.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Exists("Content/Materials/Blue.omat.json").Should().BeTrue();
        _ = workspace.Exists("Content/Materials/Red.omat.json").Should().BeFalse();
        _ = history.CanUndo.Should().BeTrue();
        _ = history.CanRedo.Should().BeFalse();
    }

    /// <summary>An undo that a later change made impossible is rejected, changes nothing and stays in the history.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task UndoMadeImpossibleByALaterChangeStaysInTheHistory()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var history = new HistoryKeeper(this);
        _ = await workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Blue.omat.json"), "Rename", history, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Write("Content/Materials/Red.omat.json", RedMaterial().Text);
        var before = workspace.Snapshot();

        Func<Task> undo = async () => await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = await undo.Should().ThrowAsync<AssetRelocationException>().ConfigureAwait(false);
        _ = workspace.Snapshot().Should().BeEquivalentTo(before);
        _ = history.CanUndo.Should().BeTrue();
    }

    /// <summary>A request that changes nothing records nothing.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RelocationThatChangesNothingRecordsNothing()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var history = new HistoryKeeper(this);

        _ = await workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Red.omat.json"), "Rename", history, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = history.CanUndo.Should().BeFalse();
    }

    /// <summary>A texture and its image move together; the descriptor keeps a valid source and a new identity.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingTextureRenamesItsImageAndRewritesIdentityAndReferrers()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await workspace.Service.RelocateAsync(Move("/Content/Textures/Wood.otex.json", "/Content/Textures/Oak.otex.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.Exists("Content/Textures/Oak.png").Should().BeTrue();
        _ = workspace.Exists("Content/Textures/Wood.png").Should().BeFalse();
        var texture = workspace.ReadJson("Content/Textures/Oak.otex.json");
        _ = texture["source"]!.GetValue<string>().Should().Be("Oak.png");
        _ = texture["virtual_path"]!.GetValue<string>().Should().Be("/Content/Textures/Oak.otex");
        _ = WoodOf(workspace.ReadJson("Content/Materials/Red.omat.json")).Should().Be(Spell("/Content/Materials/Red.omat", "/Content/Textures/Oak.otex"));
        var scene = await workspace.ReadSceneAsync("Content/Scenes/Main.oscene.json", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.Environment.PostProcess.AutoExposureMeteringMask.Should().Be(AssetPath.Parse("/Content/Textures/Oak.otex"));
    }

    /// <summary>A populated folder moves as one unit; references and file-relative sources follow it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingPopulatedFolderRewritesPrefixAndRelativeSources()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Write("Content/Props/Box.ogeo.json", BoxGeometry("Props").Text);
        _ = workspace.Write("Content/Props/mesh.bin", "bin");
        _ = workspace.Write("Content/Shared/far.otex.json", FarTexture().Text);
        _ = await workspace.Service.RelocateAsync(Move("/Content/Shared", "/Content/Props/Shared"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        var moved = workspace.ReadJson("Content/Props/Shared/far.otex.json");
        _ = moved["source"]!.GetValue<string>().Should().Be("../../Textures/Wood.png");
        _ = moved["virtual_path"]!.GetValue<string>().Should().Be("/Content/Props/Shared/far.otex");
    }

    /// <summary>Every image of a multi-source texture follows a move, and its settings stay.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingMultiSourceTextureRewritesEveryImage()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var layers = AuthoredFiles.Texture("/Content/Shared/layers.otex", "../Textures/Wood.png")
            .WithIdentityField()
            .Append("/sources", new JsonObject { ["file"] = "../Textures/Wood.png", ["layer"] = 1 })
            .Set("/mips/policy", "none");
        _ = workspace.Write("Content/Shared/layers.otex.json", layers.Text);
        _ = await workspace.Service.RelocateAsync(Move("/Content/Shared", "/Content/Textures/Shared"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        var moved = workspace.ReadJson("Content/Textures/Shared/layers.otex.json");
        _ = moved["source"]!.GetValue<string>().Should().Be("../Wood.png");
        _ = moved["sources"]![0]!["file"]!.GetValue<string>().Should().Be("../Wood.png");
        _ = moved["sources"]![0]!["layer"]!.GetValue<int>().Should().Be(1);
        _ = moved["mips"]!["policy"]!.GetValue<string>().Should().Be("none");
        _ = moved["virtual_path"]!.GetValue<string>().Should().Be("/Content/Textures/Shared/layers.otex");
    }

    /// <summary>Moving or renaming a model rewrites the source and virtual path of its import record and moves the record to the place of the new virtual path.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingAndRenamingModelRewritesItsRecordSource()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var written = workspace.WriteModel("Content/Models/Robot", "robot.gltf");
        _ = workspace.RecordOf(written).Replace(Path.DirectorySeparatorChar, '/').Should().Be("Imports/Content/Models/Robot/robot.gltf.import.json");

        _ = await workspace.Service.RelocateAsync(Move("/Content/Models/Robot", "/Content/Models/Bot"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);
        var moved = workspace.ReadRecord(written);
        _ = workspace.RecordOf(written).Replace(Path.DirectorySeparatorChar, '/').Should().Be("Imports/Content/Models/Bot/robot.gltf.import.json", "the record follows its model's virtual path");
        _ = workspace.Exists("Imports/Content/Models/Robot/robot.gltf.import.json").Should().BeFalse();
        _ = workspace.FileSystem.Directory.EnumerateDirectories(workspace.PathOf("Imports"), "*", SearchOption.AllDirectories)
            .Should().OnlyContain(folder => workspace.FileSystem.Directory.EnumerateFileSystemEntries(folder).Any(), "the record's old folders are removed once empty");
        _ = moved.Source.Should().Be("/Content/Models/Bot/robot.gltf");
        _ = moved.VirtualPath.Should().Be("/Content/Models/Bot/robot.gltf", "its products are named under its new path");
        _ = moved.SourceIdentity.Should().Be(written.SourceIdentity);

        _ = await workspace.Service.RelocateAsync(Move("/Content/Models/Bot/robot.gltf", "/Content/Models/Bot/hero.gltf"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);
        var renamed = workspace.ReadRecord(written);
        var recordPath = workspace.RecordOf(written);
        _ = recordPath.Replace(Path.DirectorySeparatorChar, '/').Should().Be("Imports/Content/Models/Bot/hero.gltf.import.json");
        _ = renamed.Source.Should().Be("/Content/Models/Bot/hero.gltf");
        _ = renamed.VirtualPath.Should().Be("/Content/Models/Bot/hero.gltf");
        _ = renamed.ResolveSource(workspace.Project)!.FullPath.Should().Be(workspace.PathOf("Content/Models/Bot/hero.gltf"));
        _ = renamed.SourceFingerprint!.Files.Should().Equal(written.SourceFingerprint!.Files);
        _ = renamed.Settings.GetProperty("unit_policy").GetString().Should().Be("normalize");
        _ = workspace.Exists("Content/Models/Bot/hero.gltf").Should().BeTrue();
        _ = workspace.FileSystem.Directory.EnumerateFiles(workspace.PathOf("Imports"), "*", SearchOption.AllDirectories).Should().ContainSingle().Which.Should().Be(workspace.PathOf(recordPath));
    }

    /// <summary>A model's preview, named after the model's file, renames and moves with it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingModelRenamesItsPreview()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.WriteModel("Content/Models/Robot", "robot.gltf");
        _ = workspace.Write("Content/Models/Robot/robot.gltf" + AssetFileTypes.ModelPreview, "{}");

        _ = await workspace.Service.RelocateAsync(Move("/Content/Models/Robot/robot.gltf", "/Content/Models/Robot/hero.gltf"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.Exists("Content/Models/Robot/hero.gltf" + AssetFileTypes.ModelPreview).Should().BeTrue();
        _ = workspace.Exists("Content/Models/Robot/robot.gltf" + AssetFileTypes.ModelPreview).Should().BeFalse();
    }

    /// <summary>Deleting a model recycles its preview with it, even when the model's folder holds other files.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task DeletingModelRecyclesItsPreview()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.WriteModel("Content/Models/Robot", "robot.gltf");
        _ = workspace.Write("Content/Models/Robot/robot.gltf" + AssetFileTypes.ModelPreview, "{}");
        _ = workspace.Write("Content/Models/Robot/notes.txt", "kept");
        var recycled = new List<string>();

        await workspace.Service.DeleteAsync(
            [AssetPath.Parse("/Content/Models/Robot/robot.gltf")],
            paths =>
            {
                recycled.AddRange(paths);
                return Task.CompletedTask;
            },
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = recycled.Should().Contain(workspace.PathOf("Content/Models/Robot/robot.gltf" + AssetFileTypes.ModelPreview))
            .And.NotContain(workspace.PathOf("Content/Models/Robot/notes.txt"));
    }

    /// <summary>
    /// A rename that changes only the case of a model's folder rewrites its record, so the model has one virtual path.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingModelFolderCaseRewritesItsRecord()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var written = workspace.WriteModel("Content/Models/robot", "robot.gltf");

        _ = await workspace.Service.RelocateAsync(Move("/Content/Models/robot", "/Content/Models/Robot"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        var renamed = workspace.ReadRecord(written);
        _ = renamed.Source.Should().Be("/Content/Models/Robot/robot.gltf");
        _ = renamed.VirtualPath.Should().Be("/Content/Models/Robot/robot.gltf");
        _ = renamed.SourceIdentity.Should().Be(written.SourceIdentity);
    }

    /// <summary>
    /// A rename that changes only the case of a model's folder renames its record's folder too, where the importer
    /// requires it; undoing the rename restores the old case.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingModelFolderCaseRenamesItsRecordFolder()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var written = workspace.WriteModel("Content/Models/robot", "robot.gltf");
        var history = new HistoryKeeper(this);

        _ = await workspace.Service.RelocateAsync(Move("/Content/Models/robot", "/Content/Models/Robot"), "Rename", history, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.RecordOf(written).Replace(Path.DirectorySeparatorChar, '/').Should().Be("Imports/Content/Models/Robot/robot.gltf.import.json");

        await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.RecordOf(written).Replace(Path.DirectorySeparatorChar, '/').Should().Be("Imports/Content/Models/robot/robot.gltf.import.json");
    }

    /// <summary>A failure part-way through a case-only folder rename restores the case of every folder it renamed.</summary>
    /// <param name="failAfterStep">The number of steps applied before the failure.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow(1)]
    [DataRow(3)]
    public async Task FailureRestoresFolderCase(int failAfterStep)
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.WriteModel("Content/Models/robot", "robot.gltf");
        var before = workspace.Snapshot();
        workspace.Service.FailAfterStep = failAfterStep;

        Func<Task> work = () => workspace.Service.RelocateAsync(Move("/Content/Models/robot", "/Content/Models/Robot"), "Rename", new HistoryKeeper(this), this.TestContext.CancellationToken);

        _ = await work.Should().ThrowAsync<AssetRelocationException>().WithMessage("*restored*").ConfigureAwait(false);
        _ = workspace.Snapshot().Keys.Order(StringComparer.Ordinal).Should().Equal(before.Keys.Order(StringComparer.Ordinal));
    }

    /// <summary>
    /// A model's products are named under it, so moving or renaming the model retargets every reference to them,
    /// surface material overrides included.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingModelRewritesReferencesToItsProducts()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.WriteModel("Content/Models/Robot", "robot.gltf");

        _ = await workspace.Service.RelocateAsync(Move("/Content/Models/Robot", "/Content/Models/Bot"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        var robot = GeometryOf(await workspace.ReadSceneAsync("Content/Scenes/Main.oscene.json", this.TestContext.CancellationToken).ConfigureAwait(false), "Robot");
        _ = robot.Geometry.Should().Be(AssetPath.Parse("/Content/Models/Bot/robot.gltf/Body.ogeo"));
        _ = MaterialOf(robot).Should().Be(AssetPath.Parse("/Content/Models/Bot/robot.gltf/Paint.omat"));
    }

    /// <summary>
    /// One product of a model is regenerated from it and has no authored place: moving, renaming or deleting it is
    /// refused with a code, without changing any file.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ModelProductOperationsAreRefused()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.WriteModel("Content/Models/Robot", "robot.gltf");
        var product = AssetPath.Parse("/Content/Models/Robot/robot.gltf/Body.ogeo");
        var before = workspace.Snapshot();

        Task MoveProduct() => workspace.Service.RelocateAsync(Move(product.VirtualPath, "/Content/Models/Body.ogeo"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken);
        Task RenameProduct() => workspace.Service.RelocateAsync(new AssetRelocationRequest { ProductRenames = [new(product, "Hull")] }, "Rename", new HistoryKeeper(this), this.TestContext.CancellationToken);
        Task DeleteProduct() => workspace.Service.DeleteAsync([product], static _ => Task.CompletedTask, this.TestContext.CancellationToken);

        foreach (var operation in new Func<Task>[] { MoveProduct, RenameProduct, DeleteProduct })
        {
            _ = (await operation.Should().ThrowAsync<AssetRelocationException>().ConfigureAwait(false))
                .Which.Code.Should().Be(AssetRelocationException.ModelProductCode);
        }

        _ = workspace.Snapshot().Should().BeEquivalentTo(before);
    }

    /// <summary>Each forbidden request is rejected before any file changes.</summary>
    /// <param name="source">The moved path.</param>
    /// <param name="target">The destination.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow("/Content", "/Stuff", DisplayName = "Mount root")]
    [DataRow("/Content/Materials/Red.omat.json", "/Content/Materials/Other.omat.json", DisplayName = "Name collision")]
    [DataRow("/Content/Materials/Red.omat.json", "/Content/Materials/Red.otex.json", DisplayName = "Type change")]
    [DataRow("/Content/Shared", "/Content/Shared/Inner", DisplayName = "Folder into itself")]
    [DataRow("/Content/Models/Robot/mesh.bin", "/Content/Models/Robot/body.bin", DisplayName = "Model bundle file")]
    [DataRow("/Content/Models/Robot/robot.gltf", "/Content/Shared/robot.gltf", DisplayName = "Model out of its bundle")]
    [DataRow("/Cooked/Content/Materials/Red.omat", "/Cooked/Content/Materials/Blue.omat", DisplayName = "Derived mount")]
    public async Task ForbiddenRequestsAreRejectedWithoutChanges(string source, string target)
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Write("Content/Materials/Other.omat.json", AuthoredFiles.Material("/Content/Materials/Other.omat").Text);
        _ = workspace.Write("Content/Shared/a.txt", "a");
        _ = workspace.WriteModel("Content/Models/Robot", "robot.gltf");
        var before = workspace.Snapshot();

        Func<Task> work = () => workspace.Service.RelocateAsync(Move(source, target), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken);

        _ = await work.Should().ThrowAsync<AssetRelocationException>().ConfigureAwait(false);
        _ = workspace.Snapshot().Should().BeEquivalentTo(before);
    }

    /// <summary>A script cannot be renamed next to a source with the same name: both would cook to one script.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ScriptRenameThatWouldCollideIsRejected()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Write("Content/Player/jump.lua", "return {}");
        _ = workspace.Write("Content/Player/move.luau", "return {}");
        var request = new AssetRelocationRequest { Moves = [new(AssetPath.Parse("/Content/Player/jump.lua"), AssetPath.Parse("/Content/Player/move.lua"))] };

        Func<Task> work = () => workspace.Service.RelocateAsync(request, "Move", new HistoryKeeper(this), this.TestContext.CancellationToken);

        _ = await work.Should().ThrowAsync<AssetRelocationException>().Where(static error => error.Code == AssetRelocationException.SourceCollisionCode).ConfigureAwait(false);
        _ = workspace.Exists("Content/Player/jump.lua").Should().BeTrue();
    }

    /// <summary>A copied script takes a name that does not collide with another source.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ScriptCopyAvoidsACollidingName()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Write("Content/Scripts/move.lua", "return {}");
        _ = workspace.Write("Content/Player/move.luau", "return {}");

        var copies = await workspace.Service.CopyAsync([AssetPath.Parse("/Content/Scripts/move.lua")], AssetPath.Parse("/Content/Player"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = copies.Should().ContainSingle().Which.Name.Should().Be("move (2).lua");
    }

    /// <summary>A copied input file keeps its compound suffix, so the copy is still an input file.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task InputCopyKeepsTheInputSuffix()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Write("Content/Input/Player.input.json", AuthoredFiles.Input("/Content/Input/Player.input").Text);

        var copies = await workspace.Service.CopyAsync([AssetPath.Parse("/Content/Input/Player.input.json")], AssetPath.Parse("/Content/Input"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = copies.Should().ContainSingle().Which.Should().Be(AssetPath.Parse("/Content/Input/Player (2).input.json"));
    }

    /// <summary>An input source's products are named under it, so moving the source retargets every reference to
    /// them, by path in another input source and by URI in a scene.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingAnInputSourceMovesItsProductsInTheirReferrers()
    {
        using var workspace = await CreateInputAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        workspace.CreateFolder("Content/Controls");

        var plan = await workspace.Service.RelocateAsync(Move("/Content/Input/Player.input.json", "/Content/Controls/Player.input.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = plan.Referrers.Should().BeEquivalentTo([AssetPath.Parse("/Content/Input/Vehicle.input.json"), AssetPath.Parse("/Content/Scenes/Main.oscene.json")]);
        _ = VehicleAction(workspace).Should().Be(Spell("/Content/Input/Vehicle.input", "/Content/Controls/Player.input/Jump.oiact"));
        _ = (await StartingContextAsync(workspace, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(AssetPath.Parse("/Content/Controls/Player.input/OnFoot.oimap"));
    }

    /// <summary>Renaming an action renames it in its source, where mappings name it, and in every referrer; undo
    /// restores both.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingAnActionRewritesItsSourceAndReferrers()
    {
        using var workspace = await CreateInputAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var history = new HistoryKeeper(this);

        var plan = await workspace.Service.RelocateAsync(Rename("/Content/Input/Player.input/Jump.oiact", "Leap"), "Rename", history, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = plan.Referrers.Should().Equal(AssetPath.Parse("/Content/Input/Vehicle.input.json"));
        var player = workspace.ReadJson("Content/Input/Player.input.json");
        _ = player["actions"]![0]!["name"]!.GetValue<string>().Should().Be("Leap");
        _ = player["contexts"]![0]!["mappings"]![0]!["action"]!.GetValue<string>().Should().Be("Leap");
        _ = VehicleAction(workspace).Should().Be(Spell("/Content/Input/Vehicle.input", "/Content/Input/Player.input/Leap.oiact"));

        await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.ReadJson("Content/Input/Player.input.json")["actions"]![0]!["name"]!.GetValue<string>().Should().Be("Jump");
        _ = VehicleAction(workspace).Should().Be(Spell("/Content/Input/Vehicle.input", "/Content/Input/Player.input/Jump.oiact"));
    }

    /// <summary>Renaming a mapping context retargets the scenes that start with it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingAContextRetargetsTheScenesThatUseIt()
    {
        using var workspace = await CreateInputAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = await workspace.Service.RelocateAsync(Rename("/Content/Input/Player.input/OnFoot.oimap", "Driving"), "Rename", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.ReadJson("Content/Input/Player.input.json")["contexts"]![0]!["name"]!.GetValue<string>().Should().Be("Driving");
        _ = (await StartingContextAsync(workspace, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(AssetPath.Parse("/Content/Input/Player.input/Driving.oimap"));
    }

    /// <summary>A rename the source cannot take changes nothing.</summary>
    /// <param name="product">The renamed product.</param>
    /// <param name="name">The new name.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow("/Content/Input/Player.input/Jump.oiact", "dash")]
    [DataRow("/Content/Input/Player.input/Fly.oiact", "Soar")]
    [DataRow("/Content/Input/Jump.oiact", "Leap")]
    public async Task ARenameTheSourceRefusesChangesNothing(string product, string name)
    {
        using var workspace = await CreateInputAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var before = workspace.Snapshot();

        Func<Task> rename = async () => await workspace.Service.RelocateAsync(Rename(product, name), "Rename", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = await rename.Should().ThrowAsync<AssetRelocationException>().ConfigureAwait(false);
        _ = workspace.Snapshot().Should().BeEquivalentTo(before);
    }

    /// <summary>Renaming a script rewrites the slots and parameters that refer to the script it cooks to.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingScriptRewritesSlotsAndParametersThatUseIt()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Write("Content/Scripts/move.lua", "return {}");
        var scripted = new Scene { Name = "Scripted" };
        var player = new SceneNode(scripted, "Player") { Id = new NodeId(Guid.Parse("3d9a2b1c-0000-4000-8000-000000000004")) };
        _ = player.AddComponent(new ScriptingComponent
        {
            Name = ScriptingComponent.DefaultName,
            Slots =
            [
                new ScriptSlot { Id = Guid.Parse("0f8fad5b-d9cb-469f-a165-70867728950e"), Script = AssetPath.Parse("/Content/Scripts/move.oscript") }
                    .WithOverride("follow", ScriptValue.FromAsset(AssetPath.Parse("/Content/Scripts/move.oscript"))),
            ],
        });
        scripted.RootNodes.Add(player);
        _ = await workspace.WriteSceneAsync("Content/Scenes/Scripted.oscene.json", scripted, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = await workspace.Service.RelocateAsync(Move("/Content/Scripts/move.lua", "/Content/Scripts/walk.lua"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        var slot = (await workspace.ReadSceneAsync("Content/Scenes/Scripted.oscene.json", this.TestContext.CancellationToken).ConfigureAwait(false))
            .RootNodes.Single().Components.OfType<ScriptingComponent>().Single().Slots.Single();
        _ = slot.Script.Should().Be(AssetPath.Parse("/Content/Scripts/walk.oscript"));
        _ = slot.Overrides["follow"].Asset.Should().Be(AssetPath.Parse("/Content/Scripts/walk.oscript"));
    }

    /// <summary>A failure part-way through restores every moved and rewritten file and leaves no journal.</summary>
    /// <param name="failAfterStep">The number of steps applied before the failure.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow(0)]
    [DataRow(1)]
    [DataRow(3)]
    public async Task FailureRestoresEveryFile(int failAfterStep)
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var before = workspace.Snapshot();
        workspace.Service.FailAfterStep = failAfterStep;

        Func<Task> work = () => workspace.Service.RelocateAsync(Move("/Content/Textures/Wood.otex.json", "/Content/Textures/Oak.otex.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken);

        _ = await work.Should().ThrowAsync<AssetRelocationException>().WithMessage("*restored*").ConfigureAwait(false);
        _ = workspace.Snapshot().Should().BeEquivalentTo(before);
    }

    /// <summary>Recovery restores a relocation that a process interruption left half-applied.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RecoveryRestoresInterruptedRelocation()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var before = workspace.Snapshot();
        var original = workspace.PathOf("Content/Materials/Red.omat.json");
        var moved = workspace.PathOf("Content/Materials/Blue.omat.json");
        var referrer = workspace.PathOf("Content/Geometry/Box.ogeo.json");
        var previous = await workspace.FileSystem.File.ReadAllBytesAsync(referrer, this.TestContext.CancellationToken).ConfigureAwait(false);
        var rewritten = BoxGeometry(material: "/Content/Materials/Blue.omat").Bytes;
        workspace.FileSystem.File.Move(original, moved);
        await workspace.FileSystem.File.WriteAllBytesAsync(referrer, rewritten, this.TestContext.CancellationToken).ConfigureAwait(false);
        var journal = new
        {
            Version = 1,
            Phase = 0,
            Moves = new[] { new { Source = original, Target = moved, IsDirectory = false } },
            CreatedDirectories = Array.Empty<string>(),
            Edits = new[] { new { Path = referrer, BeforeSha256 = Convert.ToHexString(SHA256.HashData(previous)), AfterSha256 = Convert.ToHexString(SHA256.HashData(rewritten)), Before = previous } },
        };
        var journalPath = workspace.Write(".build/relocation/interrupted/journal.json", JsonSerializer.Serialize(journal));

        await AssetRelocationTransaction.RecoverAsync(workspace.Project, workspace.Files, Microsoft.Extensions.Logging.Abstractions.NullLogger.Instance, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.FileSystem.File.Exists(journalPath).Should().BeFalse();
        _ = workspace.Snapshot().Should().BeEquivalentTo(before);
    }

    /// <summary>An unsaved document rejects a relocation, which changes no file.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task UnsavedDocumentsRejectRelocation()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.UnsavedDocuments.Setup(documents => documents.Find(It.IsAny<IEnumerable<FileSystemPath>>()))
            .Returns([UnsavedMain(workspace)]);
        var before = workspace.Snapshot();

        Func<Task> work = () => workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Blue.omat.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken);

        _ = (await work.Should().ThrowAsync<AssetRelocationException>().ConfigureAwait(false)).Which.Code.Should().Be(AssetRelocationException.UnsavedDocumentsCode);
        _ = workspace.Exists("Content/Materials/Red.omat.json").Should().BeTrue();
        _ = workspace.Snapshot().Should().BeEquivalentTo(before);
    }

    /// <summary>The unsaved documents of the project are the ones the open documents report for its root.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task FindUnsavedDocuments_ReturnsTheUnsavedDocumentsUnderTheProjectRoot()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var main = UnsavedMain(workspace);
        IEnumerable<FileSystemPath>? asked = null;
        _ = workspace.UnsavedDocuments.Setup(documents => documents.Find(It.IsAny<IEnumerable<FileSystemPath>>()))
            .Callback<IEnumerable<FileSystemPath>>(paths => asked = [.. paths])
            .Returns([main]);

        var unsaved = workspace.Service.FindUnsavedDocuments();

        _ = unsaved.Should().Equal(main);
        _ = asked.Should().Contain(workspace.Project.ProjectRoot);
    }

    /// <summary>Copies get unique names and their own identity; referrers keep pointing at the original.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CopyCreatesUniqueCopiesWithTheirOwnIdentity()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var copies = await workspace.Service.CopyAsync([AssetPath.Parse("/Content/Textures/Wood.otex.json")], AssetPath.Parse("/Content/Textures"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = copies.Should().ContainSingle().Which.Should().Be(AssetPath.Parse("/Content/Textures/Wood (2).otex.json"));
        var copy = workspace.ReadJson("Content/Textures/Wood (2).otex.json");
        _ = copy["virtual_path"]!.GetValue<string>().Should().Be("/Content/Textures/Wood (2).otex");
        _ = copy["source"]!.GetValue<string>().Should().Be("Wood (2).png");
        _ = workspace.Exists("Content/Textures/Wood (2).png").Should().BeTrue();
        _ = WoodOf(workspace.ReadJson("Content/Materials/Red.omat.json")).Should().Be(Spell("/Content/Materials/Red.omat", "/Content/Textures/Wood.otex"));
    }

    /// <summary>Delete recycles an asset's companions, and a model's whole folder with its import record.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task DeleteRecyclesCompanionsAndModelBundles()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var model = workspace.WriteModel("Content/Models/Robot", "robot.gltf");
        var recycled = new List<string>();

        await workspace.Service.DeleteAsync(
            [AssetPath.Parse("/Content/Textures/Wood.otex.json"), AssetPath.Parse("/Content/Models/Robot/robot.gltf")],
            paths =>
            {
                recycled.AddRange(paths);
                return Task.CompletedTask;
            },
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = recycled.Should().BeEquivalentTo(
            workspace.PathOf("Content/Textures/Wood.otex.json"),
            workspace.PathOf("Content/Textures/Wood.png"),
            workspace.PathOf("Content/Models/Robot"),
            workspace.PathOf(workspace.RecordOf(model)));
    }

    /// <summary>A committed relocation reports the folder it moved and the files whose references it rewrote.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RelocationReportsItsMovesAndRewrittenFiles()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Write("Content/Props/mesh.bin", "bin");
        _ = workspace.Write("Content/Shared/far.otex.json", FarTexture().Text);
        var reports = new List<AssetFilesChangedEventArgs>();
        workspace.Service.FilesChanged += (_, args) => reports.Add(args);

        _ = await workspace.Service.RelocateAsync(Move("/Content/Shared", "/Content/Props/Shared"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        var report = reports.Should().ContainSingle().Subject;
        _ = report.Moves.Should().Contain(move => move.IsDirectory && move.Source == workspace.FileOf("Content/Shared") && move.Target == workspace.FileOf("Content/Props/Shared"));
        _ = report.RewrittenFiles.Should().Contain(workspace.FileOf("Content/Props/Shared/far.otex.json"));
        _ = report.DeletedFiles.Should().BeEmpty();
        _ = report.FoldersChanged.Should().BeTrue();
    }

    /// <summary>A copy reports the files and folders it created.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CopyReportsTheCreatedFiles()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var reports = new List<AssetFilesChangedEventArgs>();
        workspace.Service.FilesChanged += (_, args) => reports.Add(args);

        _ = await workspace.Service.CopyAsync([AssetPath.Parse("/Content/Materials/Red.omat.json"), AssetPath.Parse("/Content/Textures")], AssetPath.Parse("/Content/Geometry"), this.TestContext.CancellationToken).ConfigureAwait(false);

        var report = reports.Should().ContainSingle().Subject;
        _ = report.CreatedFiles.Should().BeEquivalentTo([workspace.FileOf("Content/Geometry/Red.omat.json"), workspace.FileOf("Content/Geometry/Textures")]);
        _ = report.Moves.Should().BeEmpty();
        _ = report.FoldersChanged.Should().BeTrue("a folder was copied");
    }

    /// <summary>A committed delete reports exactly the files and folders it recycled.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task DeleteReportsTheRecycledFiles()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.WriteModel("Content/Models/Robot", "robot.gltf");
        var recycled = new List<string>();
        var reports = new List<AssetFilesChangedEventArgs>();
        workspace.Service.FilesChanged += (_, args) => reports.Add(args);

        await workspace.Service.DeleteAsync(
            [AssetPath.Parse("/Content/Textures/Wood.otex.json"), AssetPath.Parse("/Content/Models/Robot/robot.gltf")],
            paths =>
            {
                recycled.AddRange(paths);
                return Task.CompletedTask;
            },
            this.TestContext.CancellationToken).ConfigureAwait(false);

        var report = reports.Should().ContainSingle().Subject;
        _ = report.DeletedFiles.Should().BeEquivalentTo(recycled.Select(FileSystemPath.From));
        _ = report.Moves.Should().BeEmpty();
        _ = report.FoldersChanged.Should().BeTrue("the model's bundle folder was recycled");
    }

    /// <summary>The report is posted to the synchronization context the operation was started on.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task FilesChangedIsPostedToTheCallersContext()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var context = new RecordingContext();
        var reported = new TaskCompletionSource<SynchronizationContext?>(TaskCreationOptions.RunContinuationsAsynchronously);
        workspace.Service.FilesChanged += (_, _) => reported.TrySetResult(SynchronizationContext.Current);
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(context);
        Task<AssetRelocationPlan> relocation;
        try
        {
            relocation = workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Blue.omat.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken);
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }

        _ = await relocation.ConfigureAwait(false);
        _ = (await reported.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeSameAs(context);
        _ = context.Posts.Should().BePositive();
    }

    /// <summary>Deleting a texture keeps an image that another texture still uses.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task DeleteKeepsImageSharedWithAnotherTexture()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Write("Content/Textures/Oak.otex.json", AuthoredFiles.Texture("/Content/Textures/Oak.otex", "Wood.png").WithIdentityField().Set("/name", "Oak").Text);
        var recycled = new List<string>();

        await workspace.Service.DeleteAsync(
            [AssetPath.Parse("/Content/Textures/Wood.otex.json")],
            paths =>
            {
                recycled.AddRange(paths);
                return Task.CompletedTask;
            },
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = recycled.Should().BeEquivalentTo(workspace.PathOf("Content/Textures/Wood.otex.json"));
    }

    /// <summary>A six-face cube names its +X face only; the cooker reads all six, so all six go with it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task DeletingASixFaceCubeRecyclesEveryFace()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var faces = WriteSixFaceCube(workspace, "Content/Sky");
        var recycled = new List<string>();

        await workspace.Service.DeleteAsync(
            [AssetPath.Parse("/Content/Sky/Pisa.otex.json")],
            paths =>
            {
                recycled.AddRange(paths);
                return Task.CompletedTask;
            },
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = recycled.Should().BeEquivalentTo([workspace.PathOf("Content/Sky/Pisa.otex.json"), .. faces.Select(face => workspace.PathOf($"Content/Sky/{face}"))]);
    }

    /// <summary>Moving a six-face cube moves every face with its descriptor.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingASixFaceCubeMovesEveryFace()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var faces = WriteSixFaceCube(workspace, "Content/Sky");

        _ = await workspace.Service.RelocateAsync(Move("/Content/Sky/Pisa.otex.json", "/Content/Textures/Pisa.otex.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = faces.Should().NotContain(face => !workspace.Exists($"Content/Textures/{face}"));
        _ = faces.Should().NotContain(face => workspace.Exists($"Content/Sky/{face}"));
    }

    /// <summary>A request that changes nothing touches no file and starts no transaction.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RequestThatChangesNothingTouchesNoFile()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var before = workspace.Snapshot();

        var plan = await workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Red.omat.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = plan.Moves.Should().BeEmpty();
        _ = plan.RewrittenFiles.Should().BeEmpty();
        _ = workspace.Snapshot().Should().BeEquivalentTo(before);
        _ = workspace.Exists(".build/relocation").Should().BeFalse();
    }

    /// <summary>Find references lists the files that use an asset or anything inside a folder.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task FindReferrersListsUsers()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = (await workspace.Service.FindReferrersAsync(AssetPath.Parse("/Content/Materials/Red.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false))
            .Should().BeEquivalentTo([AssetPath.Parse("/Content/Geometry/Box.ogeo.json"), AssetPath.Parse("/Content/Scenes/Main.oscene.json")]);
        _ = (await workspace.Service.FindReferrersAsync(AssetPath.Parse("/Content/Textures"), this.TestContext.CancellationToken).ConfigureAwait(false))
            .Should().BeEquivalentTo([AssetPath.Parse("/Content/Materials/Red.omat.json"), AssetPath.Parse("/Content/Scenes/Main.oscene.json")]);
    }

    /// <summary>The review of a delete lists only the files outside the deleted items that use them.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task DeleteReviewListsOnlyUsersThatStay()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        var referrers = await workspace.Service.FindReferrersOutsideAsync(
            [AssetPath.Parse("/Content/Materials/Red.omat.json"), AssetPath.Parse("/Content/Geometry/Box.ogeo.json")], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = referrers.Should().Equal(AssetPath.Parse("/Content/Scenes/Main.oscene.json"));
    }

    /// <summary>A catalog change, such as an edit outside the editor, makes the next search read the files again.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CatalogChangeRefreshesTheReferrers()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var red = AssetPath.Parse("/Content/Materials/Red.omat.json");
        _ = await workspace.Service.FindReferrersAsync(red, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Write("Content/Geometry/Crate.ogeo.json", BoxGeometry(name: "Crate").Text);

        var cached = await workspace.Service.FindReferrersAsync(red, this.TestContext.CancellationToken).ConfigureAwait(false);
        workspace.ChangeInCatalog(AssetPath.Parse("/Content/Geometry/Crate.ogeo.json"));
        var fresh = await workspace.Service.FindReferrersAsync(red, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = cached.Should().NotContain(AssetPath.Parse("/Content/Geometry/Crate.ogeo.json"));
        _ = fresh.Should().Contain(AssetPath.Parse("/Content/Geometry/Crate.ogeo.json"));
    }

    /// <summary>A scene moves to any folder like any asset, and the scenes that reference it follow.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingSceneToAnotherFolderRewritesTheScenesThatReferenceIt()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var lobby = new Scene { Name = "Lobby", Id = SceneId.FromValue(Guid.Parse("0b0e6b8c-3c5a-4a0e-9a8f-1b1f2a3c4d5e")) };
        lobby.SetReferences(new SceneReferencesData { ExtraAssets = [AssetPath.Parse("/Content/Scenes/Main.oscene")] });
        _ = await workspace.WriteSceneAsync("Content/Levels/Lobby.oscene.json", lobby, this.TestContext.CancellationToken).ConfigureAwait(false);
        var history = new HistoryKeeper(this);

        var plan = await workspace.Service.RelocateAsync(Move("/Content/Scenes/Main.oscene.json", "/Content/Levels/Start.oscene.json"), "Move", history, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.Exists("Content/Scenes/Main.oscene.json").Should().BeFalse();
        _ = workspace.Exists("Content/Levels/Start.oscene.json").Should().BeTrue();
        _ = plan.Referrers.Should().Contain(AssetPath.Parse("/Content/Levels/Lobby.oscene.json"));
        _ = (await workspace.ReadSceneAsync("Content/Levels/Lobby.oscene.json", this.TestContext.CancellationToken).ConfigureAwait(false)).References.ExtraAssets.Should().Equal(AssetPath.Parse("/Content/Levels/Start.oscene"));
        _ = history.UndoStack.Should().ContainSingle();
    }

    /// <summary>A copied scene is a scene of its own: a new identity, and the name of its file.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CopyingSceneGivesTheCopyItsOwnIdentityAndName()
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        var copies = await workspace.Service.CopyAsync([AssetPath.Parse("/Content/Scenes/Main.oscene.json")], AssetPath.Parse("/Content/Scenes"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = copies.Should().ContainSingle().Which.Should().Be(AssetPath.Parse("/Content/Scenes/Main (2).oscene.json"));
        var copy = await workspace.ReadSceneAsync("Content/Scenes/Main (2).oscene.json", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = copy.Name.Should().Be("Main (2)");
        _ = copy.Id.Value.Should().NotBe(MainSceneId);
        _ = (await workspace.ReadSceneAsync("Content/Scenes/Main.oscene.json", this.TestContext.CancellationToken).ConfigureAwait(false)).Id.Value.Should().Be(MainSceneId);
    }

    /// <summary>The project's start scene cannot be deleted, alone or with the folder that holds it.</summary>
    /// <param name="source">What the delete names.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow("/Content/Levels/Start.oscene.json", DisplayName = "The scene")]
    [DataRow("/Content/Levels", DisplayName = "Its folder")]
    public async Task DeletingTheStartSceneIsRefused(string source)
    {
        using var workspace = await CreateContentAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await workspace.WriteSceneAsync("Content/Levels/Start.oscene.json", MainScene(), this.TestContext.CancellationToken).ConfigureAwait(false);
        workspace.Session.ChangeContext(workspace.Project with { DefaultSceneId = SceneId.FromValue(MainSceneId) });
        var recycled = new List<string>();

        var delete = () => workspace.Service.DeleteAsync(
            [AssetPath.Parse(source)],
            paths =>
            {
                recycled.AddRange(paths);
                return Task.CompletedTask;
            },
            this.TestContext.CancellationToken);

        _ = await delete.Should().ThrowAsync<AssetRelocationException>().WithMessage("*start scene*").ConfigureAwait(false);
        _ = recycled.Should().BeEmpty();
        _ = workspace.Exists("Content/Levels/Start.oscene.json").Should().BeTrue();
    }

    private static UnsavedDocument UnsavedMain(RelocationWorkspace workspace)
        => new(DocumentId.New(), new(workspace.FileOf("Content/Scenes/Main.oscene.json"), AssetPath.Parse("/Content/Scenes/Main.oscene")), "Main");

    private static async Task<RelocationWorkspace> CreateContentAsync(CancellationToken token)
    {
        var workspace = new RelocationWorkspace();
        _ = workspace.Write("Content/Materials/Red.omat.json", RedMaterial().Text);
        _ = workspace.Write("Content/Geometry/Box.ogeo.json", BoxGeometry().Text);
        _ = workspace.Write("Content/Geometry/mesh.bin", "bin");
        _ = workspace.Write("Content/Textures/Wood.otex.json", AuthoredFiles.Texture("/Content/Textures/Wood.otex", "Wood.png").WithIdentityField().Set("/name", "Wood").Text);
        _ = workspace.Write("Content/Textures/Wood.png", "png");
        _ = await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json", MainScene(), token).ConfigureAwait(false);
        return workspace;
    }

    private static async Task<RelocationWorkspace> CreateInputAsync(CancellationToken token)
    {
        var workspace = new RelocationWorkspace();
        var player = AuthoredFiles.Input("/Content/Input/Player.input")
            .Action("Jump", "bool")
            .Action("Dash", "bool")
            .Context("OnFoot")
            .Append("/contexts/0/mappings", new JsonObject { ["action"] = "Jump", ["slot"] = "Space" });
        var vehicle = AuthoredFiles.Input("/Content/Input/Vehicle.input").Context("Driving");
        _ = vehicle.Append("/contexts/0/mappings", new JsonObject { ["action"] = vehicle.ActionReference("/Content/Input/Player.input/Jump.oiact"), ["slot"] = "Space" });
        var main = new Scene { Name = "Main", Id = SceneId.FromValue(MainSceneId) };
        main.SetStartingInputContexts([new StartingInputContextData(AssetPath.Parse("/Content/Input/Player.input/OnFoot.oimap"))]);
        _ = workspace.Write("Content/Input/Player.input.json", player.Text);
        _ = workspace.Write("Content/Input/Vehicle.input.json", vehicle.Text);
        _ = await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json", main, token).ConfigureAwait(false);
        return workspace;
    }

    // The scene of the project: a box with a surface material override, a model's robot, and a metering mask texture.
    private static Scene MainScene()
    {
        var scene = new Scene { Name = "Main", Id = SceneId.FromValue(MainSceneId) };
        scene.RootNodes.Add(GeometryNode(scene, "Box", "3d9a2b1c-0000-4000-8000-000000000001", "/Content/Geometry/Box.ogeo", "6e1d1f0a-0000-4000-8000-000000000001", 'a', "/Content/Materials/Red.omat"));
        scene.RootNodes.Add(GeometryNode(scene, "Robot", "3d9a2b1c-0000-4000-8000-000000000002", "/Content/Models/Robot/robot.gltf/Body.ogeo", "6e1d1f0a-0000-4000-8000-000000000002", 'b', "/Content/Models/Robot/robot.gltf/Paint.omat"));
        scene.SetEnvironment(new SceneEnvironmentData { PostProcess = new PostProcessEnvironmentData { AutoExposureMeteringMask = AssetPath.Parse("/Content/Textures/Wood.otex") } });
        return scene;
    }

    private static SceneNode GeometryNode(Scene scene, string name, string node, string geometry, string surface, char revision, string material)
    {
        var result = new SceneNode(scene, name) { Id = new NodeId(Guid.Parse(node, CultureInfo.InvariantCulture)) };
        var component = new GeometryComponent { Name = "Geometry", Geometry = AssetPath.Parse(geometry) };
        component.Overrides = component.Overrides.With(SceneSchema.Geometry.Surface.Material, new PartId(SceneSchema.Geometry.Surface.Kind, Guid.Parse(surface, CultureInfo.InvariantCulture)), new string(revision, 64), AssetPath.Parse(material));
        _ = result.AddComponent(component);
        return result;
    }

    private static GeometryComponent GeometryOf(Scene scene, string node)
        => scene.AllNodes.Single(found => found.Metadata.Name == node).Components.OfType<GeometryComponent>().Single();

    private static AssetPath MaterialOf(GeometryComponent geometry) => geometry.Overrides.Assets.Single();

    private static IEnumerable<AssetPath> MaterialsOf(Scene scene)
        => scene.AllNodes.SelectMany(static node => node.Components.OfType<GeometryComponent>()).SelectMany(static geometry => geometry.Overrides.Assets);

    private static MaterialDocument RedMaterial()
        => AuthoredFiles.Material("/Content/Materials/Red.omat").Texture("base_color", "/Content/Textures/Wood.otex");

    // The box geometry in a folder, with a buffer of its own and one material.
    private static GeometryDocument BoxGeometry(string folder = "Geometry", string name = "Box", string material = "/Content/Materials/Red.omat")
    {
        var geometry = AuthoredFiles.Geometry($"/Content/{folder}/{name}.ogeo").SubmeshMaterial(0, 0, material).WithIdentityField();
        return geometry.Append("/buffers", new JsonObject { ["uri"] = "mesh.bin", ["virtual_path"] = geometry.Spell($"/Content/{folder}/{name}.obuf") });
    }

    private static TextureDocument FarTexture()
        => AuthoredFiles.Texture("/Content/Shared/far.otex", "../Textures/Wood.png").WithIdentityField();

    private static string WoodOf(JsonNode material) => material["textures"]!["base_color"]!["virtual_path"]!.GetValue<string>();

    // How a descriptor at anchor spells a reference to target, as the writers do.
    private static string Spell(string anchor, string target) => AuthoredFiles.SpellReference(AssetPath.Parse(anchor), AssetPath.Parse(target));

    private static string VehicleAction(RelocationWorkspace workspace)
        => workspace.ReadJson("Content/Input/Vehicle.input.json")["contexts"]![0]!["mappings"]![0]!["action"]!.GetValue<string>();

    private static async Task<AssetPath> StartingContextAsync(RelocationWorkspace workspace, CancellationToken token)
        => (await workspace.ReadSceneAsync("Content/Scenes/Main.oscene.json", token).ConfigureAwait(false)).StartingInputContexts.Single().Context;

    private static AssetRelocationRequest Move(string source, string target) => new() { Moves = [new(AssetPath.Parse(source), AssetPath.Parse(target))] };

    private static AssetRelocationRequest Rename(string product, string name) => new() { ProductRenames = [new(AssetPath.Parse(product), name)] };

    // A cube whose descriptor names its +X face, as a six-face import writes it; returns the face file names.
    private static string[] WriteSixFaceCube(RelocationWorkspace workspace, string folder)
    {
        string[] faces = ["Pisa_px.hdr", "Pisa_nx.hdr", "Pisa_py.hdr", "Pisa_ny.hdr", "Pisa_pz.hdr", "Pisa_nz.hdr"];
        foreach (var face in faces)
        {
            _ = workspace.Write($"{folder}/{face}", face);
        }

        _ = workspace.Write($"{folder}/Pisa.otex.json", AuthoredFiles.Texture($"/{folder}/Pisa.otex", "Pisa_px.hdr").WithIdentityField().Set("/name", "Pisa").Set("/cube/cubemap", true).Text);
        return faces;
    }

    /// <summary>Runs posted work on the thread pool with itself as the current context, counting the posts.</summary>
    private sealed class RecordingContext : SynchronizationContext
    {
        private int posts;

        public int Posts => Volatile.Read(ref this.posts);

        public override void Post(SendOrPostCallback d, object? state)
        {
            _ = Interlocked.Increment(ref this.posts);
            _ = ThreadPool.QueueUserWorkItem(_ =>
            {
                var previous = Current;
                SetSynchronizationContext(this);
                try
                {
                    d(state);
                }
                finally
                {
                    SetSynchronizationContext(previous);
                }
            });
        }
    }
}
