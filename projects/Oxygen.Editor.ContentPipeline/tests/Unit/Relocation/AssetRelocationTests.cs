// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using DroidNet.TimeMachine;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.ContentPipeline.Snapshots;
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
          "lods": [ { "submeshes": [ { "slot_id": "a", "material_ref": "/Content/Materials/Red.omat", "views": [] } ] } ] }
        """;

    private const string Scene = """
        { "Id": "6a1f2c0e-6c1e-4a59-9a77-1f6a3e4b2d10", "Name": "Main",
          "RootNodes": [ { "Name": "Box", "Components": [ { "$type": "GeometryComponent",
            "GeometryUri": "asset:///Content/Geometry/Box.ogeo",
            "OverrideSlots": [ { "$type": "MaterialsSlot", "GeometryUri": "asset:///Content/Geometry/Box.ogeo", "MaterialUri": "asset:///Content/Materials/Red.omat" } ],
            "TargetedOverrides": [ { "OverrideSlots": [ { "$type": "MaterialsSlot", "GeometryUri": "asset:///Content/Geometry/Box.ogeo", "MaterialUri": "asset:///Content/Materials/Red.omat" } ] } ] } ] },
            { "Name": "Robot", "Components": [ { "$type": "GeometryComponent", "GeometryUri": "asset:///Content/Geometry/Robot/robot_Body.ogeo" } ] } ],
          "Environment": { "PostProcess": { "AutoExposureMeteringMask": "asset:///Content/Textures/Wood.otex" } } }
        """;

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Renaming a material rewrites scene slots (component and targeted) and geometry material references.</summary>
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
        _ = workspace.ReadJson("Content/Scenes/Main.oscene.json")["RootNodes"]![0]!["Components"]![0]!["TargetedOverrides"]![0]!["OverrideSlots"]![0]!["MaterialUri"]!.GetValue<string>()
            .Should().Be("asset:///Content/Materials/Blue.omat");
        _ = workspace.ReadJson("Content/Geometry/Box.ogeo.json")["lods"]![0]!["submeshes"]![0]!["material_ref"]!.GetValue<string>().Should().Be("/Content/Materials/Blue.omat");
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
        var before = Snapshot(workspace.Root);

        Func<Task> undo = async () => await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = await undo.Should().ThrowAsync<AssetRelocationException>().ConfigureAwait(false);
        _ = Snapshot(workspace.Root).Should().BeEquivalentTo(before);
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

    /// <summary>Renaming an output group rewrites the sidecar, moves real group folders and rewrites output references.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingImportGroupRewritesSidecarFoldersAndReferences()
    {
        using var workspace = CreateContent();
        _ = workspace.WriteModel("Content/SourceMedia/DCC/Robot", "robot.gltf", "Robot");
        _ = workspace.Write("Content/Materials/Robot/Paint.omat.json", """{ "name": "Paint" }""");
        var request = new AssetRelocationRequest { GroupMoves = [new(AssetPath.Parse("/Content/SourceMedia/DCC/Robot/robot.gltf"), "Hero")] };

        var plan = await workspace.Service.RelocateAsync(request, "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.ReadSettings("Content/SourceMedia/DCC/Robot/robot.gltf.import.json").OutputDirectory.Should().Be("Hero");
        _ = workspace.Exists("Content/Materials/Hero/Paint.omat.json").Should().BeTrue();
        _ = workspace.Read("Content/Scenes/Main.oscene.json").Should().Contain("asset:///Content/Geometry/Hero/robot_Body.ogeo");
        _ = plan.GroupFolders.Should().HaveCount(3);
        _ = plan.Reverse.GroupMoves.Should().ContainSingle().Which.Group.Should().Be("Robot");
    }

    /// <summary>A real folder that holds an output group moves the matching folder under the other type folders.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingFolderHoldingGroupExpandsToAllTypeFolders()
    {
        using var workspace = CreateContent();
        _ = workspace.WriteModel("Content/SourceMedia/DCC/Robot", "robot.gltf", "Vehicles/Robot");
        _ = workspace.Write("Content/Geometry/Vehicles/readme.txt", "notes");
        _ = workspace.Write("Content/Scenes/Vehicles/keep.txt", "notes");

        _ = await workspace.Service.RelocateAsync(Move("/Content/Geometry/Vehicles", "/Content/Geometry/Cars"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.ReadSettings("Content/SourceMedia/DCC/Robot/robot.gltf.import.json").OutputDirectory.Should().Be("Cars/Robot");
        _ = workspace.Exists("Content/Scenes/Cars/keep.txt").Should().BeTrue();
    }

    /// <summary>Moving a model's folder rewrites the bundle root; renaming the model renames its sidecar.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingAndRenamingModelKeepsItsSidecarValid()
    {
        using var workspace = CreateContent();
        _ = workspace.WriteModel("Content/SourceMedia/DCC/Robot", "robot.gltf", "Robot");

        _ = await workspace.Service.RelocateAsync(Move("/Content/SourceMedia/DCC/Robot", "/Content/SourceMedia/Bot"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.ReadSettings("Content/SourceMedia/Bot/robot.gltf.import.json").BundleRoot.Should().Be("Content/SourceMedia/Bot");

        _ = await workspace.Service.RelocateAsync(Move("/Content/SourceMedia/Bot/robot.gltf", "/Content/SourceMedia/Bot/hero.gltf"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);
        var settings = workspace.ReadSettings("Content/SourceMedia/Bot/hero.gltf.import.json");
        _ = settings.PrimaryRelativePath.Should().Be("hero.gltf");
        _ = settings.Files.Should().BeEquivalentTo("hero.gltf", "mesh.bin");
        _ = workspace.Exists("Content/SourceMedia/Bot/robot.gltf.import.json").Should().BeFalse();
    }

    /// <summary>Each forbidden request is rejected before any file changes.</summary>
    /// <param name="source">The moved path.</param>
    /// <param name="target">The destination.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow("/Content/Materials", "/Content/Mats", DisplayName = "Importer type folder")]
    [DataRow("/Content", "/Stuff", DisplayName = "Mount root")]
    [DataRow("/Content/Materials/Red.omat.json", "/Content/Materials/Other.omat.json", DisplayName = "Name collision")]
    [DataRow("/Content/Materials/Red.omat.json", "/Content/Materials/Red.otex.json", DisplayName = "Type change")]
    [DataRow("/Content/Shared", "/Content/Shared/Inner", DisplayName = "Folder into itself")]
    [DataRow("/Content/SourceMedia/DCC/Robot/mesh.bin", "/Content/SourceMedia/DCC/Robot/body.bin", DisplayName = "Model bundle file")]
    [DataRow("/Content/SourceMedia/DCC/Robot/robot.gltf.import.json", "/Content/SourceMedia/DCC/Robot/x.gltf.import.json", DisplayName = "Import sidecar")]
    [DataRow("/Content/SourceMedia/DCC/Robot/robot.gltf", "/Content/Shared/robot.gltf", DisplayName = "Model out of its bundle")]
    [DataRow("/Content/Geometry/Robot/robot_Body.ogeo", "/Content/Geometry/X.ogeo", DisplayName = "Cooked-only output")]
    [DataRow("/Cooked/Content/Materials/Red.omat", "/Cooked/Content/Materials/Blue.omat", DisplayName = "Derived mount")]
    public async Task ForbiddenRequestsAreRejectedWithoutChanges(string source, string target)
    {
        using var workspace = CreateContent();
        _ = workspace.Write("Content/Materials/Other.omat.json", """{ "name": "Other" }""");
        _ = workspace.Write("Content/Shared/a.txt", "a");
        _ = workspace.WriteModel("Content/SourceMedia/DCC/Robot", "robot.gltf", "Robot");
        var before = Snapshot(workspace.Root);

        Func<Task> work = () => workspace.Service.RelocateAsync(Move(source, target), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken);

        _ = await work.Should().ThrowAsync<AssetRelocationException>().ConfigureAwait(false);
        _ = Snapshot(workspace.Root).Should().BeEquivalentTo(before);
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

        _ = await work.Should().ThrowAsync<AssetRelocationException>().WithMessage("*move.luau*same script*").ConfigureAwait(false);
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

    /// <summary>Renaming a script rewrites the slots and parameters that refer to the script it cooks to.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingScriptRewritesSlotsAndParametersThatUseIt()
    {
        using var workspace = CreateContent();
        _ = workspace.Write("Content/Scripts/move.lua", "return {}");
        const string scripted = """
            { "Name": "Scripted", "RootNodes": [ { "Name": "Player", "Components": [ { "$type": "Scripting", "Name": "Scripting",
              "Slots": [ { "Id": "0f8fad5b-d9cb-469f-a165-70867728950e", "Script": "asset:///Content/Scripts/move.oscript",
                "Overrides": { "follow": { "Type": "asset", "Value": "asset:///Content/Scripts/move.oscript" } } } ] } ] } ] }
            """;
        _ = workspace.Write("Content/Scenes/Scripted.oscene.json", scripted);

        _ = await workspace.Service.RelocateAsync(Move("/Content/Scripts/move.lua", "/Content/Scripts/walk.lua"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        var slot = workspace.ReadJson("Content/Scenes/Scripted.oscene.json")["RootNodes"]![0]!["Components"]![0]!["Slots"]![0]!;
        _ = slot["Script"]!.GetValue<string>().Should().Be("asset:///Content/Scripts/walk.oscript");
        _ = slot["Overrides"]!["follow"]!["Value"]!.GetValue<string>().Should().Be("asset:///Content/Scripts/walk.oscript");
    }

    /// <summary>An output group cannot overlap another import's group.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task GroupOverlappingAnotherImportIsRejected()
    {
        using var workspace = CreateContent();
        _ = workspace.WriteModel("Content/SourceMedia/DCC/Robot", "robot.gltf", "Robot");
        _ = workspace.WriteModel("Content/SourceMedia/DCC/Car", "car.gltf", "Car");
        var request = new AssetRelocationRequest { GroupMoves = [new(AssetPath.Parse("/Content/SourceMedia/DCC/Robot/robot.gltf"), "Car/Robot")] };

        Func<Task> work = () => workspace.Service.RelocateAsync(request, "Move", new HistoryKeeper(this), this.TestContext.CancellationToken);

        _ = await work.Should().ThrowAsync<AssetRelocationException>().WithMessage("*overlap*").ConfigureAwait(false);
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
        var before = Snapshot(workspace.Root);
        workspace.Service.FailAfterStep = failAfterStep;

        Func<Task> work = () => workspace.Service.RelocateAsync(Move("/Content/Textures/Wood.otex.json", "/Content/Textures/Oak.otex.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken);

        _ = await work.Should().ThrowAsync<AssetRelocationException>().WithMessage("*restored*").ConfigureAwait(false);
        _ = Snapshot(workspace.Root).Should().BeEquivalentTo(before);
    }

    /// <summary>Recovery restores a relocation that a process interruption left half-applied.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RecoveryRestoresInterruptedRelocation()
    {
        using var workspace = CreateContent();
        var before = Snapshot(workspace.Root);
        var original = workspace.PathOf("Content/Materials/Red.omat.json");
        var moved = workspace.PathOf("Content/Materials/Blue.omat.json");
        var referrer = workspace.PathOf("Content/Geometry/Box.ogeo.json");
        var previous = await File.ReadAllBytesAsync(referrer, this.TestContext.CancellationToken).ConfigureAwait(false);
        var rewritten = Encoding.UTF8.GetBytes(Geometry.Replace("Red.omat", "Blue.omat", StringComparison.Ordinal));
        File.Move(original, moved);
        await File.WriteAllBytesAsync(referrer, rewritten, this.TestContext.CancellationToken).ConfigureAwait(false);
        var journal = new
        {
            Version = 1,
            Phase = 0,
            Moves = new[] { new { Source = original, Target = moved, IsDirectory = false } },
            CreatedDirectories = Array.Empty<string>(),
            Edits = new[] { new { Path = referrer, BeforeSha256 = Convert.ToHexString(SHA256.HashData(previous)), AfterSha256 = Convert.ToHexString(SHA256.HashData(rewritten)), Before = previous } },
        };
        var journalPath = workspace.Write(".build/relocation/interrupted/journal.json", JsonSerializer.Serialize(journal));

        await AssetRelocationTransaction.RecoverAsync(workspace.Root, workspace.Files, Microsoft.Extensions.Logging.Abstractions.NullLogger.Instance, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = File.Exists(journalPath).Should().BeFalse();
        _ = Snapshot(workspace.Root).Should().BeEquivalentTo(before);
    }

    /// <summary>Unsaved documents reject a relocation and are named.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task UnsavedDocumentsRejectRelocation()
    {
        using var workspace = CreateContent();
        var path = workspace.FileOf("Content/Scenes/Main.oscene.json");
        using var registration = workspace.Documents.Register(path, _ => Task.FromResult<CookDocumentReadLease?>(null));
        registration.UpdateState(new(path, "Main", IsDirty: true, SavedContentHash: string.Empty));

        Func<Task> work = () => workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Blue.omat.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken);

        _ = await work.Should().ThrowAsync<AssetRelocationException>().WithMessage("*Main*").ConfigureAwait(false);
        _ = workspace.Exists("Content/Materials/Red.omat.json").Should().BeTrue();
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

    /// <summary>Delete recycles an asset's companions, and a model's whole bundle folder.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task DeleteRecyclesCompanionsAndModelBundles()
    {
        using var workspace = CreateContent();
        _ = workspace.WriteModel("Content/SourceMedia/DCC/Robot", "robot.gltf", "Robot");
        var recycled = new List<string>();

        await workspace.Service.DeleteAsync(
            [AssetPath.Parse("/Content/Textures/Wood.otex.json"), AssetPath.Parse("/Content/SourceMedia/DCC/Robot/robot.gltf")],
            paths =>
            {
                recycled.AddRange(paths);
                return Task.CompletedTask;
            },
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = recycled.Should().BeEquivalentTo(
            workspace.PathOf("Content/Textures/Wood.otex.json"),
            workspace.PathOf("Content/Textures/Wood.png"),
            workspace.PathOf("Content/SourceMedia/DCC/Robot"));
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
        _ = workspace.WriteModel("Content/SourceMedia/DCC/Robot", "robot.gltf", "Robot");
        var recycled = new List<string>();
        var reports = new List<AssetFilesChangedEventArgs>();
        workspace.Service.FilesChanged += (_, args) => reports.Add(args);

        await workspace.Service.DeleteAsync(
            [AssetPath.Parse("/Content/Textures/Wood.otex.json"), AssetPath.Parse("/Content/SourceMedia/DCC/Robot/robot.gltf")],
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

    /// <summary>A request that changes nothing touches no file and starts no transaction.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RequestThatChangesNothingTouchesNoFile()
    {
        using var workspace = CreateContent();
        var before = Snapshot(workspace.Root);

        var plan = await workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Red.omat.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = plan.Moves.Should().BeEmpty();
        _ = plan.RewrittenFiles.Should().BeEmpty();
        _ = Snapshot(workspace.Root).Should().BeEquivalentTo(before);
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
        workspace.Context.Activate(workspace.Project with { DefaultSceneId = SceneId.FromValue(Guid.Parse("6a1f2c0e-6c1e-4a59-9a77-1f6a3e4b2d10")) });
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

    /// <summary>A delete names the open documents it would close, and which of them have unsaved edits.</summary>
    [TestMethod]
    public void FindOpenDocuments_NamesTheOpenDocumentsInsideTheDeletedItems()
    {
        using var workspace = CreateContent();
        using var scene = workspace.Documents.Register(workspace.FileOf("Content/Scenes/Main.oscene.json"), _ => Task.FromResult<CookDocumentReadLease?>(null));
        scene.UpdateState(new CookDocumentState(workspace.FileOf("Content/Scenes/Main.oscene.json"), "Main", IsDirty: true, string.Empty));
        using var material = workspace.Documents.Register(workspace.FileOf("Content/Materials/Red.omat.json"), _ => Task.FromResult<CookDocumentReadLease?>(null));
        material.UpdateState(new CookDocumentState(workspace.FileOf("Content/Materials/Red.omat.json"), "Red", IsDirty: false, string.Empty));

        var open = workspace.Service.FindOpenDocuments([AssetPath.Parse("/Content/Scenes")]);

        _ = open.Should().Equal(("Main", true));
    }

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

    private static AssetRelocationRequest Move(string source, string target) => new() { Moves = [new(AssetPath.Parse(source), AssetPath.Parse(target))] };

    private static Dictionary<string, string> Snapshot(string root)
        => Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)
            .Where(path => !Path.GetRelativePath(root, path).StartsWith(".build", StringComparison.Ordinal))
            .ToDictionary(path => Path.GetRelativePath(root, path), File.ReadAllText, StringComparer.Ordinal);

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
