// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Documents;
using DroidNet.TimeMachine;
using Moq;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Relocation;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class AssetRelocationTests
{
    private const string Material = """{ "name": "Red", "textures": { "base_color": { "virtual_path": "/Content/Textures/Wood.otex" } } }""";

    private const string Geometry = """
        { "name": "Box", "virtual_path": "/Content/Geometry/Box.ogeo",
          "buffers": [ { "uri": "mesh.bin", "virtual_path": "/Content/Geometry/Box.obuf" } ],
          "lods": [ { "submeshes": [ { "surface_id": "a", "material_ref": "/Content/Materials/Red.omat", "views": [] } ] } ] }
        """;

    private const string Scene = """
        { "Id": "6a1f2c0e-6c1e-4a59-9a77-1f6a3e4b2d10", "Name": "Main",
          "RootNodes": [ { "Id": "3d9a2b1c-0000-4000-8000-000000000001", "Components": [ { "$type": "Metadata", "Name": "Box" }, { "$type": "Transform", "Name": "Transform" }, { "$type": "NodeFlags", "Name": "Rendering" }, { "$type": "GeometryComponent",
            "GeometryUri": "asset:///Content/Geometry/Box.ogeo",
            "Overrides": { "Parts": [ { "Kind": "surface", "Id": "6e1d1f0a-0000-4000-8000-000000000001", "LayoutRevision": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "Values": { "/material": "asset:///Content/Materials/Red.omat" } } ] } }, { "$type": "Hierarchy", "Name": "Hierarchy" } ] },
            { "Id": "3d9a2b1c-0000-4000-8000-000000000002", "Components": [ { "$type": "Metadata", "Name": "Robot" }, { "$type": "Transform", "Name": "Transform" }, { "$type": "NodeFlags", "Name": "Rendering" }, { "$type": "GeometryComponent", "GeometryUri": "asset:///Content/Models/Robot/robot.gltf/Body.ogeo",
              "Overrides": { "Parts": [ { "Kind": "surface", "Id": "6e1d1f0a-0000-4000-8000-000000000002", "LayoutRevision": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "Values": { "/material": "asset:///Content/Models/Robot/robot.gltf/Paint.omat" } } ] } }, { "$type": "Hierarchy", "Name": "Hierarchy" } ] } ],
          "Environment": { "PostProcess": { "AutoExposureMeteringMask": "asset:///Content/Textures/Wood.otex" } } }
        """;

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Renaming a material rewrites scene surface overrides and geometry material references.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingMaterialRewritesEveryReferenceInItsForm()
    {
        using var workspace = CreateContent();
        var plan = await workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Blue.omat.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.Exists("Content/Materials/Red.omat.json").Should().BeFalse();
        _ = workspace.Exists("Content/Materials/Blue.omat.json").Should().BeTrue();
        _ = plan.Referrers.Should().BeEquivalentTo([AssetPath.Parse("/Content/Geometry/Box.ogeo.json"), AssetPath.Parse("/Content/Scenes/Main.oscene.json")]);
        var scene = workspace.Read("Content/Scenes/Main.oscene.json");
        _ = scene.Should().NotContain("Red.omat").And.Contain("asset:///Content/Materials/Blue.omat");
        _ = workspace.ReadJson("Content/Scenes/Main.oscene.json")["RootNodes"]![0]!["Components"]![3]!["Overrides"]!["Parts"]![0]!["Values"]!["/material"]!.GetValue<string>()
            .Should().Be("asset:///Content/Materials/Blue.omat");
        _ = workspace.ReadJson("Content/Geometry/Box.ogeo.json")["lods"]![0]!["submeshes"]![0]!["material_ref"]!.GetValue<string>().Should().Be("/Content/Materials/Blue.omat");
    }

    /// <summary>
    /// A relocation rewrites the fields a format names as references, and leaves text that only looks like a path:
    /// a node's name, a script's string parameter, or a field of a format that is not a reference.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RelocationRewritesReferenceFieldsAndLeavesText()
    {
        using var workspace = CreateContent();
        const string named = """
            { "Id": "0b0e6b8c-3c5a-4a0e-9a8f-1b1f2a3c4d5e", "Name": "Named",
              "RootNodes": [ { "Id": "3d9a2b1c-0000-4000-8000-000000000003", "Components": [ { "$type": "Metadata", "Name": "asset:///Content/Materials/Red.omat" }, { "$type": "Transform", "Name": "Transform" }, { "$type": "NodeFlags", "Name": "Rendering" }, { "$type": "Scripting", "Name": "Scripting", "Slots": [ {
                "Id": "0f8fad5b-d9cb-469f-a165-70867728950e", "Script": "asset:///Content/Scripts/paint.oscript",
                "Overrides": {
                  "label": { "Type": "string", "Value": "asset:///Content/Materials/Red.omat" },
                  "paint": { "Type": "asset", "Value": "asset:///Content/Materials/Red.omat" } } } ] } ] } ] }
            """;
        const string box = """
            { "name": "/Content/Materials/Red.omat", "virtual_path": "/Content/Shapes/Box.ocshape", "shape_type": "box", "material_ref": "/Content/Materials/Red.opmat" }
            """;
        _ = workspace.Write("Content/Scenes/Named.oscene.json", named);
        _ = workspace.Write("Content/Shapes/Box.ocshape.json", box);

        var plan = await workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Blue.omat.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = plan.Referrers.Should().Contain(AssetPath.Parse("/Content/Scenes/Named.oscene.json")).And.NotContain(AssetPath.Parse("/Content/Shapes/Box.ocshape.json"));
        var components = workspace.ReadJson("Content/Scenes/Named.oscene.json")["RootNodes"]![0]!["Components"]!;
        _ = components[0]!["Name"]!.GetValue<string>().Should().Be("asset:///Content/Materials/Red.omat", "a node's name is text");
        var overrides = components[3]!["Slots"]![0]!["Overrides"]!;
        _ = overrides["label"]!["Value"]!.GetValue<string>().Should().Be("asset:///Content/Materials/Red.omat", "a string parameter is text");
        _ = overrides["paint"]!["Value"]!.GetValue<string>().Should().Be("asset:///Content/Materials/Blue.omat");
        _ = workspace.ReadJson("Content/Shapes/Box.ocshape.json")["name"]!.GetValue<string>().Should().Be("/Content/Materials/Red.omat");
    }

    /// <summary>A relocation undoes and redoes through the history it was recorded in.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RelocationUndoesAndRedoesThroughItsHistory()
    {
        using var workspace = CreateContent();
        var history = new HistoryKeeper(this);
        _ = await workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Blue.omat.json"), "Rename", history, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = history.UndoStack.Should().ContainSingle().Which.Key.Should().Be("Rename");

        await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Exists("Content/Materials/Red.omat.json").Should().BeTrue();
        _ = workspace.Read("Content/Scenes/Main.oscene.json").Should().Contain("asset:///Content/Materials/Red.omat").And.NotContain("Blue.omat");
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
        using var workspace = CreateContent();
        var history = new HistoryKeeper(this);
        _ = await workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Blue.omat.json"), "Rename", history, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Write("Content/Materials/Red.omat.json", Material);
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
        using var workspace = CreateContent();
        var history = new HistoryKeeper(this);

        _ = await workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Red.omat.json"), "Rename", history, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = history.CanUndo.Should().BeFalse();
    }

    /// <summary>A texture and its image move together; the descriptor keeps a valid source and a new identity.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingTextureRenamesItsImageAndRewritesIdentityAndReferrers()
    {
        using var workspace = CreateContent();
        _ = await workspace.Service.RelocateAsync(Move("/Content/Textures/Wood.otex.json", "/Content/Textures/Oak.otex.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.Exists("Content/Textures/Oak.png").Should().BeTrue();
        _ = workspace.Exists("Content/Textures/Wood.png").Should().BeFalse();
        var texture = workspace.ReadJson("Content/Textures/Oak.otex.json");
        _ = texture["source"]!.GetValue<string>().Should().Be("Oak.png");
        _ = texture["virtual_path"]!.GetValue<string>().Should().Be("/Content/Textures/Oak.otex");
        _ = workspace.Read("Content/Materials/Red.omat.json").Should().Contain("/Content/Textures/Oak.otex");
        _ = workspace.Read("Content/Scenes/Main.oscene.json").Should().Contain("asset:///Content/Textures/Oak.otex");
    }

    /// <summary>A populated folder moves as one unit; references and file-relative sources follow it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingPopulatedFolderRewritesPrefixAndRelativeSources()
    {
        using var workspace = CreateContent();
        _ = workspace.Write("Content/Props/Box.ogeo.json", Geometry.Replace("/Content/Geometry/", "/Content/Props/", StringComparison.Ordinal));
        _ = workspace.Write("Content/Props/mesh.bin", "bin");
        _ = workspace.Write("Content/Shared/far.otex.json", """{ "virtual_path": "/Content/Shared/far.otex", "source": "../Textures/Wood.png" }""");
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
        using var workspace = CreateContent();
        const string layers = """
            { "virtual_path": "/Content/Shared/layers.otex", "source": "../Textures/Wood.png",
              "sources": [ { "file": "../Textures/Wood.png", "layer": 1 } ], "mips": { "policy": "none" } }
            """;
        _ = workspace.Write("Content/Shared/layers.otex.json", layers);
        _ = await workspace.Service.RelocateAsync(Move("/Content/Shared", "/Content/Textures/Shared"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        var moved = workspace.ReadJson("Content/Textures/Shared/layers.otex.json");
        _ = moved["source"]!.GetValue<string>().Should().Be("../Wood.png");
        _ = moved["sources"]![0]!["file"]!.GetValue<string>().Should().Be("../Wood.png");
        _ = moved["sources"]![0]!["layer"]!.GetValue<int>().Should().Be(1);
        _ = moved["mips"]!["policy"]!.GetValue<string>().Should().Be("none");
        _ = moved["virtual_path"]!.GetValue<string>().Should().Be("/Content/Textures/Shared/layers.otex");
    }

    /// <summary>Moving or renaming a model rewrites the source of its import record, which stays where it is.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingAndRenamingModelRewritesItsRecordSource()
    {
        using var workspace = CreateContent();
        var written = workspace.WriteModel("Content/Models/Robot", "robot.gltf");
        var recordPath = workspace.RecordOf(written);

        _ = await workspace.Service.RelocateAsync(Move("/Content/Models/Robot", "/Content/Models/Bot"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);
        var moved = workspace.ReadRecord(written);
        _ = moved.Source.Should().Be("../Content/Models/Bot/robot.gltf");
        _ = moved.VirtualPath.Should().Be("/Content/Models/Bot/robot.gltf", "its products are named under its new path");
        _ = moved.SourceIdentity.Should().Be(written.SourceIdentity);

        _ = await workspace.Service.RelocateAsync(Move("/Content/Models/Bot/robot.gltf", "/Content/Models/Bot/hero.gltf"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);
        var renamed = workspace.ReadRecord(written);
        _ = renamed.Source.Should().Be("../Content/Models/Bot/hero.gltf");
        _ = renamed.VirtualPath.Should().Be("/Content/Models/Bot/hero.gltf");
        _ = renamed.ResolveSource(workspace.PathOf(recordPath)).Should().Be(workspace.PathOf("Content/Models/Bot/hero.gltf"));
        _ = renamed.SourceFingerprint!.Files.Should().Equal(written.SourceFingerprint!.Files);
        _ = renamed.Settings.GetProperty("unit_policy").GetString().Should().Be("normalize");
        _ = workspace.Exists("Content/Models/Bot/hero.gltf").Should().BeTrue();
        _ = workspace.FileSystem.Directory.EnumerateFiles(workspace.PathOf("imports")).Should().ContainSingle().Which.Should().Be(workspace.PathOf(recordPath));
    }

    /// <summary>A model's preview, named after the model's file, renames and moves with it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingModelRenamesItsPreview()
    {
        using var workspace = CreateContent();
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
        using var workspace = CreateContent();
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
        using var workspace = CreateContent();
        var written = workspace.WriteModel("Content/Models/robot", "robot.gltf");

        _ = await workspace.Service.RelocateAsync(Move("/Content/Models/robot", "/Content/Models/Robot"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        var renamed = workspace.ReadRecord(written);
        _ = renamed.Source.Should().Be("../Content/Models/Robot/robot.gltf");
        _ = renamed.VirtualPath.Should().Be("/Content/Models/Robot/robot.gltf");
        _ = renamed.SourceIdentity.Should().Be(written.SourceIdentity);
    }

    /// <summary>
    /// A model's products are named under it, so moving or renaming the model retargets every reference to them,
    /// surface material overrides included.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingModelRewritesReferencesToItsProducts()
    {
        using var workspace = CreateContent();
        _ = workspace.WriteModel("Content/Models/Robot", "robot.gltf");

        _ = await workspace.Service.RelocateAsync(Move("/Content/Models/Robot", "/Content/Models/Bot"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        var robot = workspace.ReadJson("Content/Scenes/Main.oscene.json")["RootNodes"]![1]!["Components"]![3]!;
        _ = robot["GeometryUri"]!.GetValue<string>().Should().Be("asset:///Content/Models/Bot/robot.gltf/Body.ogeo");
        var values = robot["Overrides"]!["Parts"]![0]!["Values"]!;
        _ = values["/material"]!.GetValue<string>().Should().Be("asset:///Content/Models/Bot/robot.gltf/Paint.omat");
    }

    /// <summary>
    /// One product of a model is regenerated from it and has no authored place: moving, renaming or deleting it is
    /// refused with a code, without changing any file.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ModelProductOperationsAreRefused()
    {
        using var workspace = CreateContent();
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
        using var workspace = CreateContent();
        _ = workspace.Write("Content/Materials/Other.omat.json", """{ "name": "Other" }""");
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
        using var workspace = CreateContent();
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
        using var workspace = CreateContent();
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
        using var workspace = CreateContent();
        _ = workspace.Write("Content/Input/Player.input.json", "{}");

        var copies = await workspace.Service.CopyAsync([AssetPath.Parse("/Content/Input/Player.input.json")], AssetPath.Parse("/Content/Input"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = copies.Should().ContainSingle().Which.Should().Be(AssetPath.Parse("/Content/Input/Player (2).input.json"));
    }

    /// <summary>An input source's products are named under it, so moving the source retargets every reference to
    /// them, by path in another input source and by URI in a scene.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingAnInputSourceMovesItsProductsInTheirReferrers()
    {
        using var workspace = CreateInput();
        workspace.CreateFolder("Content/Controls");

        var plan = await workspace.Service.RelocateAsync(Move("/Content/Input/Player.input.json", "/Content/Controls/Player.input.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = plan.Referrers.Should().BeEquivalentTo([AssetPath.Parse("/Content/Input/Vehicle.input.json"), AssetPath.Parse("/Content/Scenes/Main.oscene.json")]);
        _ = VehicleAction(workspace).Should().Be("/Content/Controls/Player.input/Jump.oiact");
        _ = StartingContext(workspace).Should().Be("asset:///Content/Controls/Player.input/OnFoot.oimap");
    }

    /// <summary>Renaming an action renames it in its source, where mappings name it, and in every referrer; undo
    /// restores both.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingAnActionRewritesItsSourceAndReferrers()
    {
        using var workspace = CreateInput();
        var history = new HistoryKeeper(this);

        var plan = await workspace.Service.RelocateAsync(Rename("/Content/Input/Player.input/Jump.oiact", "Leap"), "Rename", history, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = plan.Referrers.Should().Equal(AssetPath.Parse("/Content/Input/Vehicle.input.json"));
        var player = workspace.ReadJson("Content/Input/Player.input.json");
        _ = player["actions"]![0]!["name"]!.GetValue<string>().Should().Be("Leap");
        _ = player["contexts"]![0]!["mappings"]![0]!["action"]!.GetValue<string>().Should().Be("Leap");
        _ = VehicleAction(workspace).Should().Be("/Content/Input/Player.input/Leap.oiact");

        await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.ReadJson("Content/Input/Player.input.json")["actions"]![0]!["name"]!.GetValue<string>().Should().Be("Jump");
        _ = VehicleAction(workspace).Should().Be("/Content/Input/Player.input/Jump.oiact");
    }

    /// <summary>Renaming a mapping context retargets the scenes that start with it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingAContextRetargetsTheScenesThatUseIt()
    {
        using var workspace = CreateInput();

        _ = await workspace.Service.RelocateAsync(Rename("/Content/Input/Player.input/OnFoot.oimap", "Driving"), "Rename", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.ReadJson("Content/Input/Player.input.json")["contexts"]![0]!["name"]!.GetValue<string>().Should().Be("Driving");
        _ = StartingContext(workspace).Should().Be("asset:///Content/Input/Player.input/Driving.oimap");
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
        using var workspace = CreateInput();
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
        using var workspace = CreateContent();
        _ = workspace.Write("Content/Scripts/move.lua", "return {}");
        const string scripted = """
            { "Name": "Scripted", "RootNodes": [ { "Id": "3d9a2b1c-0000-4000-8000-000000000004", "Components": [ { "$type": "Metadata", "Name": "Player" }, { "$type": "Transform", "Name": "Transform" }, { "$type": "NodeFlags", "Name": "Rendering" }, { "$type": "Scripting", "Name": "Scripting",
              "Slots": [ { "Id": "0f8fad5b-d9cb-469f-a165-70867728950e", "Script": "asset:///Content/Scripts/move.oscript",
                "Overrides": { "follow": { "Type": "asset", "Value": "asset:///Content/Scripts/move.oscript" } } } ] } ] } ] }
            """;
        _ = workspace.Write("Content/Scenes/Scripted.oscene.json", scripted);

        _ = await workspace.Service.RelocateAsync(Move("/Content/Scripts/move.lua", "/Content/Scripts/walk.lua"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        var slot = workspace.ReadJson("Content/Scenes/Scripted.oscene.json")["RootNodes"]![0]!["Components"]![3]!["Slots"]![0]!;
        _ = slot["Script"]!.GetValue<string>().Should().Be("asset:///Content/Scripts/walk.oscript");
        _ = slot["Overrides"]!["follow"]!["Value"]!.GetValue<string>().Should().Be("asset:///Content/Scripts/walk.oscript");
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
        using var workspace = CreateContent();
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
        using var workspace = CreateContent();
        var before = workspace.Snapshot();
        var original = workspace.PathOf("Content/Materials/Red.omat.json");
        var moved = workspace.PathOf("Content/Materials/Blue.omat.json");
        var referrer = workspace.PathOf("Content/Geometry/Box.ogeo.json");
        var previous = await workspace.FileSystem.File.ReadAllBytesAsync(referrer, this.TestContext.CancellationToken).ConfigureAwait(false);
        var rewritten = Encoding.UTF8.GetBytes(Geometry.Replace("Red.omat", "Blue.omat", StringComparison.Ordinal));
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
        using var workspace = CreateContent();
        _ = workspace.UnsavedDocuments.Setup(documents => documents.Find(It.IsAny<IEnumerable<FileSystemPath>>()))
            .Returns([UnsavedMain(workspace)]);
        var before = workspace.Snapshot();

        Func<Task> work = () => workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Blue.omat.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken);

        _ = (await work.Should().ThrowAsync<AssetRelocationException>().ConfigureAwait(false)).Which.Code.Should().Be(AssetRelocationException.UnsavedDocumentsCode);
        _ = workspace.Exists("Content/Materials/Red.omat.json").Should().BeTrue();
        _ = workspace.Snapshot().Should().BeEquivalentTo(before);
    }

    /// <summary>The unsaved documents of the project are the ones the open documents report for its root.</summary>
    [TestMethod]
    public void FindUnsavedDocuments_ReturnsTheUnsavedDocumentsUnderTheProjectRoot()
    {
        using var workspace = CreateContent();
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
        using var workspace = CreateContent();
        var copies = await workspace.Service.CopyAsync([AssetPath.Parse("/Content/Textures/Wood.otex.json")], AssetPath.Parse("/Content/Textures"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = copies.Should().ContainSingle().Which.Should().Be(AssetPath.Parse("/Content/Textures/Wood (2).otex.json"));
        var copy = workspace.ReadJson("Content/Textures/Wood (2).otex.json");
        _ = copy["virtual_path"]!.GetValue<string>().Should().Be("/Content/Textures/Wood (2).otex");
        _ = copy["source"]!.GetValue<string>().Should().Be("Wood (2).png");
        _ = workspace.Exists("Content/Textures/Wood (2).png").Should().BeTrue();
        _ = workspace.Read("Content/Materials/Red.omat.json").Should().Contain("/Content/Textures/Wood.otex\"");
    }

    /// <summary>Delete recycles an asset's companions, and a model's whole folder with its import record.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task DeleteRecyclesCompanionsAndModelBundles()
    {
        using var workspace = CreateContent();
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
        using var workspace = CreateContent();
        _ = workspace.Write("Content/Props/mesh.bin", "bin");
        _ = workspace.Write("Content/Shared/far.otex.json", """{ "virtual_path": "/Content/Shared/far.otex", "source": "../Textures/Wood.png" }""");
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
        using var workspace = CreateContent();
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
        using var workspace = CreateContent();
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
        using var workspace = CreateContent();
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
        using var workspace = CreateContent();
        _ = workspace.Write("Content/Textures/Oak.otex.json", """{ "name": "Oak", "virtual_path": "/Content/Textures/Oak.otex", "source": "Wood.png" }""");
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
        using var workspace = CreateContent();
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
        using var workspace = CreateContent();
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
        using var workspace = CreateContent();
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
        using var workspace = CreateContent();
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
        using var workspace = CreateContent();

        var referrers = await workspace.Service.FindReferrersOutsideAsync(
            [AssetPath.Parse("/Content/Materials/Red.omat.json"), AssetPath.Parse("/Content/Geometry/Box.ogeo.json")], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = referrers.Should().Equal(AssetPath.Parse("/Content/Scenes/Main.oscene.json"));
    }

    /// <summary>A catalog change, such as an edit outside the editor, makes the next search read the files again.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CatalogChangeRefreshesTheReferrers()
    {
        using var workspace = CreateContent();
        var red = AssetPath.Parse("/Content/Materials/Red.omat.json");
        _ = await workspace.Service.FindReferrersAsync(red, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Write("Content/Geometry/Crate.ogeo.json", Geometry.Replace("Box", "Crate", StringComparison.Ordinal));

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
        using var workspace = CreateContent();
        _ = workspace.Write("Content/Levels/Lobby.oscene.json", """{ "Id": "0b0e6b8c-3c5a-4a0e-9a8f-1b1f2a3c4d5e", "Name": "Lobby", "RootNodes": [], "References": { "ExtraAssets": [ "asset:///Content/Scenes/Main.oscene" ] } }""");
        var history = new HistoryKeeper(this);

        var plan = await workspace.Service.RelocateAsync(Move("/Content/Scenes/Main.oscene.json", "/Content/Levels/Start.oscene.json"), "Move", history, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.Exists("Content/Scenes/Main.oscene.json").Should().BeFalse();
        _ = workspace.Exists("Content/Levels/Start.oscene.json").Should().BeTrue();
        _ = plan.Referrers.Should().Contain(AssetPath.Parse("/Content/Levels/Lobby.oscene.json"));
        _ = workspace.ReadJson("Content/Levels/Lobby.oscene.json")["References"]!["ExtraAssets"]![0]!.GetValue<string>().Should().Be("asset:///Content/Levels/Start.oscene");
        _ = history.UndoStack.Should().ContainSingle();
    }

    /// <summary>A copied scene is a scene of its own: a new identity, and the name of its file.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CopyingSceneGivesTheCopyItsOwnIdentityAndName()
    {
        using var workspace = CreateContent();

        var copies = await workspace.Service.CopyAsync([AssetPath.Parse("/Content/Scenes/Main.oscene.json")], AssetPath.Parse("/Content/Scenes"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = copies.Should().ContainSingle().Which.Should().Be(AssetPath.Parse("/Content/Scenes/Main (2).oscene.json"));
        var copy = workspace.ReadJson("Content/Scenes/Main (2).oscene.json");
        _ = copy["Name"]!.GetValue<string>().Should().Be("Main (2)");
        _ = Guid.Parse(copy["Id"]!.GetValue<string>()).Should().NotBe(Guid.Parse("6a1f2c0e-6c1e-4a59-9a77-1f6a3e4b2d10"));
        _ = workspace.ReadJson("Content/Scenes/Main.oscene.json")["Id"]!.GetValue<string>().Should().Be("6a1f2c0e-6c1e-4a59-9a77-1f6a3e4b2d10");
    }

    /// <summary>The project's start scene cannot be deleted, alone or with the folder that holds it.</summary>
    /// <param name="source">What the delete names.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow("/Content/Levels/Start.oscene.json", DisplayName = "The scene")]
    [DataRow("/Content/Levels", DisplayName = "Its folder")]
    public async Task DeletingTheStartSceneIsRefused(string source)
    {
        using var workspace = CreateContent();
        _ = workspace.Write("Content/Levels/Start.oscene.json", Scene);
        workspace.Session.ChangeContext(workspace.Project with { DefaultSceneId = SceneId.FromValue(Guid.Parse("6a1f2c0e-6c1e-4a59-9a77-1f6a3e4b2d10")) });
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

    private static RelocationWorkspace CreateContent()
    {
        var workspace = new RelocationWorkspace();
        _ = workspace.Write("Content/Materials/Red.omat.json", Material);
        _ = workspace.Write("Content/Geometry/Box.ogeo.json", Geometry);
        _ = workspace.Write("Content/Geometry/mesh.bin", "bin");
        _ = workspace.Write("Content/Textures/Wood.otex.json", """{ "name": "Wood", "virtual_path": "/Content/Textures/Wood.otex", "source": "Wood.png" }""");
        _ = workspace.Write("Content/Textures/Wood.png", "png");
        _ = workspace.Write("Content/Scenes/Main.oscene.json", Scene);
        return workspace;
    }

    private static RelocationWorkspace CreateInput()
    {
        var workspace = new RelocationWorkspace();
        const string player = """
            {
              "actions": [ { "name": "Jump", "type": "bool" }, { "name": "Dash", "type": "bool" } ],
              "contexts": [ { "name": "OnFoot", "mappings": [ { "action": "Jump", "slot": "Space" } ] } ]
            }
            """;
        const string vehicle = """
            { "contexts": [ { "name": "Driving", "mappings": [ { "action": "/Content/Input/Player.input/Jump.oiact", "slot": "Space" } ] } ] }
            """;
        const string main = """
            { "Id": "6a1f2c0e-6c1e-4a59-9a77-1f6a3e4b2d10", "Name": "Main", "RootNodes": [],
              "StartingInputContexts": [ { "Context": "asset:///Content/Input/Player.input/OnFoot.oimap" } ] }
            """;
        _ = workspace.Write("Content/Input/Player.input.json", player);
        _ = workspace.Write("Content/Input/Vehicle.input.json", vehicle);
        _ = workspace.Write("Content/Scenes/Main.oscene.json", main);
        return workspace;
    }

    private static string VehicleAction(RelocationWorkspace workspace)
        => workspace.ReadJson("Content/Input/Vehicle.input.json")["contexts"]![0]!["mappings"]![0]!["action"]!.GetValue<string>();

    private static string StartingContext(RelocationWorkspace workspace)
        => workspace.ReadJson("Content/Scenes/Main.oscene.json")["StartingInputContexts"]![0]!["Context"]!.GetValue<string>();

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

        _ = workspace.Write($"{folder}/Pisa.otex.json", $$"""{ "name": "Pisa", "virtual_path": "/{{folder}}/Pisa.otex", "source": "Pisa_px.hdr", "cube": { "cubemap": true } }""");
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
