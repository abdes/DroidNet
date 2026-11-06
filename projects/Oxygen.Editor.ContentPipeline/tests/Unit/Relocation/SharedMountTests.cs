// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using DroidNet.TimeMachine;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Models;
using Oxygen.Editor.Models.Testing;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Relocation;

/// <summary>
/// A folder outside the project, mounted as a source mount, is relocated like <c>Content</c>; a shared asset moved while
/// the project was closed leaves its references as written, and the reference check lists them, in the Problems pane
/// and in <c>.build/references.json</c>, until they are rewritten.
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class SharedMountTests
{
    private const string Studio = @"D:\Studio\Shared";
    private const string RawMedia = @"D:\Studio\RawMedia";

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Renaming a texture in a mount outside the project moves its files there and rewrites its referrers.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingATextureInAMountOutsideTheProject_MovesItThereAndRewritesItsReferrers()
    {
        using var workspace = CreateWorkspace();

        _ = await workspace.Service.RelocateAsync(Move("/Studio/Textures/Brick.otex.json", "/Studio/Textures/Stone.otex.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.FileSystem.File.Exists(Path.Combine(Studio, "Textures", "Stone.otex.json")).Should().BeTrue();
        _ = workspace.FileSystem.File.Exists(Path.Combine(Studio, "Textures", "Stone.png")).Should().BeTrue();
        _ = workspace.FileSystem.File.Exists(Path.Combine(Studio, "Textures", "Brick.otex.json")).Should().BeFalse();
        _ = workspace.ReadJson("Content/Materials/Wall.omat.json")["textures"]!["base_color"]!["virtual_path"]!.GetValue<string>()
            .Should().Be(AuthoredFiles.SpellReference(AssetPath.Parse("/Content/Materials/Wall.omat"), AssetPath.Parse("/Studio/Textures/Stone.otex")));
    }

    /// <summary>
    /// A shared texture moved while the project was closed: its material keeps the old path, the check lists it with
    /// its field and code and writes it to <c>.build/references.json</c>, and after the rewrite the list is clear.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ASharedAssetMovedWhileTheProjectWasClosed_IsListedUntilItsReferencesAreRewritten()
    {
        using var workspace = CreateWorkspace();
        workspace.FileSystem.File.Move(Path.Combine(Studio, "Textures", "Brick.otex.json"), Path.Combine(Studio, "Textures", "Walls.otex.json"));
        workspace.FileSystem.File.Move(Path.Combine(Studio, "Textures", "Brick.png"), Path.Combine(Studio, "Textures", "Walls.png"));
        using var check = new ReferenceCheck(workspace.Session, NullLogger<ReferenceCheck>.Instance);

        var unresolved = await check.RunAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        var reference = unresolved.Should().ContainSingle().Subject;
        _ = reference.File.Should().Be(workspace.FileOf("Content/Materials/Wall.omat.json"));
        _ = reference.Property.Should().Be("$.textures.base_color.virtual_path");
        _ = reference.Written.Should().Be(AuthoredFiles.SpellReference(AssetPath.Parse("/Content/Materials/Wall.omat"), AssetPath.Parse("/Studio/Textures/Brick.otex")));
        _ = reference.Resolved.Should().Be("/Studio/Textures/Brick.otex");
        _ = reference.Code.Should().Be(AssetIdentityDiagnosticCodes.ResolveMissing);
        _ = check.Problems.Should().ContainSingle().Which.Code.Should().Be(AssetIdentityDiagnosticCodes.ResolveMissing);
        var written = ReadReferences(workspace);
        _ = written["schema_version"]!.GetValue<int>().Should().Be(ReferenceCheck.ReferencesFileVersion);
        var entry = written["references"]!.AsArray().Should().ContainSingle().Subject!;
        _ = entry["file"]!.GetValue<string>().Should().Be("/Content/Materials/Wall.omat.json");
        _ = entry["property"]!.GetValue<string>().Should().Be("$.textures.base_color.virtual_path");
        _ = entry["written"]!.GetValue<string>().Should().Be("/Studio/Textures/Brick.otex");
        _ = entry["resolved"]!.GetValue<string>().Should().Be("/Studio/Textures/Brick.otex");
        _ = entry["code"]!.GetValue<string>().Should().Be(AssetIdentityDiagnosticCodes.ResolveMissing);

        _ = workspace.Write("Content/Materials/Wall.omat.json", WallMaterial("/Studio/Textures/Walls.otex").Text);
        _ = await check.RunAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = check.Unresolved.Should().BeEmpty();
        _ = check.Problems.Should().BeEmpty();
        _ = ReadReferences(workspace)["references"]!.AsArray().Should().BeEmpty();
    }

    /// <summary>A reference into a mount this machine has not bound is listed with the mount's code, not as missing.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AReferenceIntoAnUnboundMount_IsListedWithTheMountsCode()
    {
        using var workspace = CreateWorkspace();
        workspace.FileSystem.File.Delete(workspace.PathOf(ProjectMounts.FileName));
        workspace.Session.ChangeContext(TestProject.Open(TestProject.InfoOf(workspace.Project), workspace.FileSystem).Context);

        var unresolved = await ReferenceCheck.FindAsync(workspace.Project, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = unresolved.Should().ContainSingle().Which.Code.Should().Be(MountCodes.Unbound);
    }

    // Content holds a material that references a texture of Studio, a folder outside the project.
    /// <summary>A reference the grammar refuses names nothing: it is listed as written, with the refusal's code.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AReferenceTheGrammarRefuses_IsListedAsWrittenWithItsCode()
    {
        using var workspace = CreateWorkspace();
        var text = WallMaterial("/Content/Textures/Brick.otex").Text.Replace("../Textures/Brick.otex", "/Content/Textures/Brick.otex", StringComparison.Ordinal);
        _ = workspace.Write("Content/Materials/Wall.omat.json", text);

        var reference = (await ReferenceCheck.FindAsync(workspace.Project, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().ContainSingle().Subject;

        _ = reference.Written.Should().Be("/Content/Textures/Brick.otex");
        _ = reference.Resolved.Should().BeNull();
        _ = reference.Code.Should().Be("vpath.reference_self_absolute");
    }

    /// <summary>Shared content is read by other projects, which do not have this project's own mounts.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AReferenceFromASharedMountIntoAnOwnMount_IsListed()
    {
        using var workspace = CreateWorkspace();
        _ = workspace.Write("Content/Textures/Wood.otex.json", AuthoredFiles.Texture("/Content/Textures/Wood.otex", "Wood.png").Text);
        _ = workspace.Write("Content/Textures/Wood.png", "png");
        workspace.WriteOutside(Path.Combine(Studio, "Materials", "Plank.omat.json"), AuthoredFiles.Material("/Studio/Materials/Plank.omat").Texture("base_color", "/Content/Textures/Wood.otex").Text);

        var reference = (await ReferenceCheck.FindAsync(workspace.Project, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().ContainSingle().Subject;

        _ = reference.File.FullPath.Should().Be(Path.Combine(Studio, "Materials", "Plank.omat.json"));
        _ = reference.Resolved.Should().Be("/Content/Textures/Wood.otex");
        _ = reference.Code.Should().Be(ReferenceCheck.ReferenceIntoProjectMount);
    }

    /// <summary>A record's source that the grammar refuses is listed with the record, not lost with it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ARecordSourceTheGrammarRefuses_IsListed()
    {
        using var workspace = CreateWorkspace();
        var record = workspace.WriteModel("Content/Models/Robot", "robot.gltf");
        var path = workspace.PathOf(workspace.RecordOf(record));
        workspace.FileSystem.File.WriteAllText(path, workspace.FileSystem.File.ReadAllText(path).Replace("\"source\": \"robot.gltf\"", "\"source\": \"../../../../Robot.gltf\"", StringComparison.Ordinal));

        var reference = (await ReferenceCheck.FindAsync(workspace.Project, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().ContainSingle().Subject;

        _ = reference.Written.Should().Be("../../../../Robot.gltf");
        _ = reference.Code.Should().Be("vpath.reference_outside_mount");
    }

    /// <summary>
    /// A material moved into a shared mount takes what it needs from own mounts, with their companions, at their paths below
    /// the mount; references inside the moved set stay relative, the game's own become absolute, and Undo restores all of it.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingIntoASharedMount_TakesWhatItNeedsAndUndoRestores()
    {
        using var workspace = CreateWorkspace();
        _ = workspace.Write("Content/Textures/Rust.otex.json", AuthoredFiles.Texture("/Content/Textures/Rust.otex", "Rust.png").Text);
        _ = workspace.Write("Content/Textures/Rust.png", "png");
        _ = workspace.Write("Content/Materials/Rust.omat.json", AuthoredFiles.Material("/Content/Materials/Rust.omat").Texture("base_color", "/Content/Textures/Rust.otex").Text);
        _ = workspace.Write("Content/Materials/Other.omat.json", AuthoredFiles.Material("/Content/Materials/Other.omat").Texture("base_color", "/Content/Textures/Rust.otex").Text);
        workspace.WriteOutside(Path.Combine(Studio, "Materials", "keep.txt"), "folder");
        var history = new HistoryKeeper(this);
        var before = workspace.ReadJson("Content/Materials/Rust.omat.json").ToJsonString();

        var plan = await workspace.Service.RelocateAsync(Move("/Content/Materials/Rust.omat.json", "/Studio/Materials/Rust.omat.json"), "Move", history, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = plan.ClosureMoves.Should().ContainSingle().Which.Target.Should().Be(AssetPath.Parse("/Studio/Textures/Rust.otex.json"));
        _ = plan.SharedMounts.Should().Equal("Studio");
        _ = workspace.FileSystem.File.Exists(Path.Combine(Studio, "Textures", "Rust.png")).Should().BeTrue("a companion goes with its asset");
        var moved = JsonNode.Parse(workspace.FileSystem.File.ReadAllText(Path.Combine(Studio, "Materials", "Rust.omat.json")))!;
        _ = moved["textures"]!["base_color"]!["virtual_path"]!.GetValue<string>().Should().Be("../Textures/Rust.otex");
        _ = workspace.ReadJson("Content/Materials/Other.omat.json")["textures"]!["base_color"]!["virtual_path"]!.GetValue<string>().Should().Be("/Studio/Textures/Rust.otex");

        await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.ReadJson("Content/Materials/Rust.omat.json").ToJsonString().Should().Be(before);
        _ = workspace.Exists("Content/Textures/Rust.png").Should().BeTrue();
        _ = workspace.FileSystem.File.Exists(Path.Combine(Studio, "Textures", "Rust.otex.json")).Should().BeFalse();
        _ = workspace.ReadJson("Content/Materials/Other.omat.json")["textures"]!["base_color"]!["virtual_path"]!.GetValue<string>().Should().Be("../Textures/Rust.otex");
    }

    /// <summary>A model moved into a shared mount takes its record and companions.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingAModelIntoASharedMount_TakesItsRecordAndCompanions()
    {
        using var workspace = CreateWorkspace();
        var model = workspace.WriteModel("Content/Props/Lamp", "Lamp.gltf");
        workspace.WriteOutside(Path.Combine(Studio, "Props", "keep.txt"), "folder");

        _ = await workspace.Service.RelocateAsync(Move("/Content/Props/Lamp/Lamp.gltf", "/Studio/Props/Lamp.gltf"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.FileSystem.File.Exists(Path.Combine(Studio, "Props", "Lamp.gltf")).Should().BeTrue();
        _ = workspace.FileSystem.File.Exists(Path.Combine(Studio, "Props", "mesh.bin")).Should().BeTrue();
        var record = workspace.ReadRecord(model);
        _ = record.Identity.Should().Be(AssetPath.Parse("/Studio/Props/Lamp.gltf"));
        _ = record.Source.Should().Be("Lamp.gltf");
        _ = record.SourceIdentity.Should().Be(model.SourceIdentity);
    }

    /// <summary>P3: a model reading a source in another mount lists it in the review; keeping it keeps the reference.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingAModelWithASourceInAnotherMount_ListsItAndCanKeepTheReference()
    {
        using var workspace = CreateWorkspaceWithRawModel(out var model);
        var move = Move("/Content/Props/V1.gltf", "/Studio/Props/V1.gltf");

        var review = await workspace.Service.PreviewAsync(move, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await workspace.Service.RelocateAsync(move, "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = review.ReferencedSources.Should().Equal(AssetPath.Parse("/RawMedia/Cars/V1.gltf"));
        _ = workspace.ReadRecord(model).Source.Should().Be("/RawMedia/Cars/V1.gltf");
        _ = workspace.FileSystem.File.Exists(Path.Combine(Studio, "Props", "V1", "V1.gltf")).Should().BeFalse();
    }

    /// <summary>P3: copying the source in makes it the model's companion, leaves the original, and Undo and Redo replay it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingAModelWithItsSourceCopiedIn_OwnsTheCopyAndUndoRemovesIt()
    {
        using var workspace = CreateWorkspaceWithRawModel(out var model);
        var history = new HistoryKeeper(this);
        var reports = new List<AssetFilesChangedEventArgs>();
        workspace.Service.FilesChanged += (_, args) => reports.Add(args);
        var copy = Path.Combine(Studio, "Props", "V1", "V1.gltf");
        var companion = Path.Combine(Studio, "Props", "V1", "mesh.bin");

        var plan = await workspace.Service.RelocateAsync(Move("/Content/Props/V1.gltf", "/Studio/Props/V1.gltf") with { CopiesSourcesIn = true }, "Move", history, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = plan.ReferencedSources.Should().BeEmpty();
        _ = workspace.FileSystem.File.Exists(copy).Should().BeTrue();
        _ = workspace.FileSystem.File.Exists(companion).Should().BeTrue();
        _ = workspace.FileSystem.File.Exists(Path.Combine(RawMedia, "Cars", "V1.gltf")).Should().BeTrue("the original stays where other models can read it");
        var moved = workspace.ReadRecord(model);
        _ = moved.Identity.Should().Be(AssetPath.Parse("/Studio/Props/V1.gltf"));
        _ = moved.Source.Should().Be("V1/V1.gltf");
        _ = moved.SourceIdentity.Should().Be(model.SourceIdentity);
        _ = reports.Should().ContainSingle().Which.CreatedFiles.Should().BeEquivalentTo([FileSystemPath.From(copy), FileSystemPath.From(companion)]);

        await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.FileSystem.File.Exists(copy).Should().BeFalse();
        _ = workspace.FileSystem.File.Exists(companion).Should().BeFalse();
        var restored = workspace.ReadRecord(model);
        _ = restored.Identity.Should().Be(AssetPath.Parse("/Content/Props/V1.gltf"));
        _ = restored.Source.Should().Be("/RawMedia/Cars/V1.gltf");
        _ = reports[^1].DeletedFiles.Should().BeEquivalentTo([FileSystemPath.From(copy), FileSystemPath.From(companion)]);

        await history.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.FileSystem.File.Exists(copy).Should().BeTrue();
        _ = workspace.ReadRecord(model).Source.Should().Be("V1/V1.gltf");
    }

    /// <summary>A failure after the copies removes them and restores everything else.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AFailedMoveWithSourcesCopiedIn_RemovesTheCopies()
    {
        using var workspace = CreateWorkspaceWithRawModel(out var model);
        workspace.Service.FailAfterStep = 3;

        var move = () => workspace.Service.RelocateAsync(Move("/Content/Props/V1.gltf", "/Studio/Props/V1.gltf") with { CopiesSourcesIn = true }, "Move", new HistoryKeeper(this), this.TestContext.CancellationToken);

        _ = await move.Should().ThrowAsync<AssetRelocationException>().ConfigureAwait(false);
        _ = workspace.FileSystem.Directory.Exists(Path.Combine(Studio, "Props", "V1")).Should().BeFalse();
        _ = workspace.ReadRecord(model).Source.Should().Be("/RawMedia/Cars/V1.gltf");
    }

    /// <summary>An undo refuses to remove a copy that holds work the original does not.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task UndoingACopyIn_WhenTheCopyChanged_IsRefused()
    {
        using var workspace = CreateWorkspaceWithRawModel(out _);
        var history = new HistoryKeeper(this);
        _ = await workspace.Service.RelocateAsync(Move("/Content/Props/V1.gltf", "/Studio/Props/V1.gltf") with { CopiesSourcesIn = true }, "Move", history, this.TestContext.CancellationToken).ConfigureAwait(false);
        var companion = Path.Combine(Studio, "Props", "V1", "mesh.bin");
        workspace.WriteOutside(companion, "edited");

        var undo = () => history.UndoAsync(this.TestContext.CancellationToken).AsTask();

        _ = await undo.Should().ThrowAsync<AssetRelocationException>().WithMessage("*changed after it was copied*").ConfigureAwait(false);
        _ = workspace.FileSystem.File.ReadAllText(companion).Should().Be("edited");
    }

    /// <summary>A copy that would overwrite a file refuses the whole move.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CopyingASourceIn_WhenItsPlaceIsTaken_IsRefused()
    {
        using var workspace = CreateWorkspaceWithRawModel(out var model);
        workspace.WriteOutside(Path.Combine(Studio, "Props", "V1", "mesh.bin"), "taken");

        var move = () => workspace.Service.RelocateAsync(Move("/Content/Props/V1.gltf", "/Studio/Props/V1.gltf") with { CopiesSourcesIn = true }, "Move", new HistoryKeeper(this), this.TestContext.CancellationToken);

        _ = await move.Should().ThrowAsync<AssetRelocationException>().WithMessage("*mesh.bin*already exists*").ConfigureAwait(false);
        _ = workspace.ReadRecord(model).Identity.Should().Be(AssetPath.Parse("/Content/Props/V1.gltf"));
    }

    /// <summary>What the closure would overwrite refuses the whole move.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingIntoASharedMount_WhenTheClosureCollides_IsRefused()
    {
        using var workspace = CreateWorkspace();
        _ = workspace.Write("Content/Textures/Brick.otex.json", AuthoredFiles.Texture("/Content/Textures/Brick.otex", "Brick.png").Text);
        _ = workspace.Write("Content/Textures/Brick.png", "png");
        _ = workspace.Write("Content/Materials/Red.omat.json", AuthoredFiles.Material("/Content/Materials/Red.omat").Texture("base_color", "/Content/Textures/Brick.otex").Text);
        workspace.WriteOutside(Path.Combine(Studio, "Materials", "keep.txt"), "folder");

        var move = () => workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Studio/Materials/Red.omat.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken);

        _ = await move.Should().ThrowAsync<AssetRelocationException>().WithMessage("*already exists*").ConfigureAwait(false);
        _ = workspace.Exists("Content/Materials/Red.omat.json").Should().BeTrue();
    }

    /// <summary>An asset moved out of a shared mount into an own one would leave shared content referencing it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingIntoAnOwnMount_WhatSharedContentReferences_IsRefused()
    {
        using var workspace = CreateWorkspace();
        workspace.WriteOutside(Path.Combine(Studio, "Materials", "Plank.omat.json"), AuthoredFiles.Material("/Studio/Materials/Plank.omat").Texture("base_color", "/Studio/Textures/Brick.otex").Text);
        _ = workspace.Write("Content/Textures/keep.txt", "folder");

        var move = () => workspace.Service.RelocateAsync(Move("/Studio/Textures/Brick.otex.json", "/Content/Textures/Brick.otex.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken);

        var refusal = (await move.Should().ThrowAsync<AssetRelocationException>().ConfigureAwait(false)).Which;
        _ = refusal.Code.Should().Be(ReferenceCheck.ReferenceIntoProjectMount);
        _ = workspace.FileSystem.File.Exists(Path.Combine(Studio, "Textures", "Brick.otex.json")).Should().BeTrue();
    }

    /// <summary>A copy into a shared mount copies what it needs from own mounts and references those copies; the originals stay.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CopyingIntoASharedMount_CopiesWhatItNeeds()
    {
        using var workspace = CreateWorkspace();
        _ = workspace.Write("Content/Textures/Rust.otex.json", AuthoredFiles.Texture("/Content/Textures/Rust.otex", "Rust.png").Text);
        _ = workspace.Write("Content/Textures/Rust.png", "png");
        _ = workspace.Write("Content/Materials/Rust.omat.json", AuthoredFiles.Material("/Content/Materials/Rust.omat").Texture("base_color", "/Content/Textures/Rust.otex").Text);
        workspace.WriteOutside(Path.Combine(Studio, "Materials", "keep.txt"), "folder");

        _ = await workspace.Service.CopyAsync([AssetPath.Parse("/Content/Materials/Rust.omat.json")], AssetPath.Parse("/Studio/Materials"), this.TestContext.CancellationToken).ConfigureAwait(false);

        var copy = JsonNode.Parse(workspace.FileSystem.File.ReadAllText(Path.Combine(Studio, "Materials", "Rust.omat.json")))!;
        _ = copy["textures"]!["base_color"]!["virtual_path"]!.GetValue<string>().Should().Be("../Textures/Rust.otex");
        _ = workspace.FileSystem.File.Exists(Path.Combine(Studio, "Textures", "Rust.otex.json")).Should().BeTrue();
        _ = workspace.FileSystem.File.Exists(Path.Combine(Studio, "Textures", "Rust.png")).Should().BeTrue();
        _ = workspace.Exists("Content/Textures/Rust.otex.json").Should().BeTrue();
    }

    /// <summary>An operation in a shared mount names it, since other projects that mount it are not updated.</summary>
    [TestMethod]
    public void TheSharedMountsOfAnOperationAreNamed()
    {
        using var workspace = CreateWorkspace();

        _ = workspace.Service.FindSharedMounts([AssetPath.Parse("/Studio/Textures/Brick.otex"), AssetPath.Parse("/Content/Materials/Wall.omat")]).Should().Equal("Studio");
    }

    private static RelocationWorkspace CreateWorkspace()
    {
        var workspace = new RelocationWorkspace();
        workspace.MountOutside("Studio", Studio);
        workspace.WriteOutside(Path.Combine(Studio, "Textures", "Brick.otex.json"), AuthoredFiles.Texture("/Studio/Textures/Brick.otex", "Brick.png").Set("/name", "Brick").Text);
        workspace.WriteOutside(Path.Combine(Studio, "Textures", "Brick.png"), "png");
        _ = workspace.Write("Content/Materials/Wall.omat.json", WallMaterial("/Studio/Textures/Brick.otex").Text);
        return workspace;
    }

    // A model in the project reading a source that has a companion file in a second shared mount.
    private static RelocationWorkspace CreateWorkspaceWithRawModel(out ModelImportSettings model)
    {
        var workspace = CreateWorkspace();
        workspace.MountOutside("RawMedia", RawMedia);
        workspace.WriteOutside(Path.Combine(RawMedia, "Cars", "V1.gltf"), "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"mesh.bin\"}]}");
        workspace.WriteOutside(Path.Combine(RawMedia, "Cars", "mesh.bin"), "bin");
        workspace.WriteOutside(Path.Combine(Studio, "Props", "keep.txt"), "folder");
        _ = workspace.Write("Content/Props/keep.txt", "folder");
        model = ModelRecords.Create("/Content/Props/V1.gltf", "/RawMedia/Cars/V1.gltf", ModelImportSettings.DefaultSettings("V1")) with
        {
            SourceFingerprint = new(2, new string('a', 64), [new("mesh.bin", 3, new string('b', 64))]),
        };
        _ = ModelRecords.Write(workspace.Project, model);
        return workspace;
    }

    private static MaterialDocument WallMaterial(string texture) => AuthoredFiles.Material("/Content/Materials/Wall.omat").Texture("base_color", texture);

    private static JsonNode ReadReferences(RelocationWorkspace workspace)
        => JsonNode.Parse(workspace.FileSystem.File.ReadAllText(ProjectOutputFolders.BuildPath(workspace.Root, ReferenceCheck.FileName)))!;

    private static AssetRelocationRequest Move(string source, string target) => new() { Moves = [new(AssetPath.Parse(source), AssetPath.Parse(target))] };
}
