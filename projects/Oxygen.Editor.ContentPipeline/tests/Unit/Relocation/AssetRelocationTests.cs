// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.ContentPipeline.Snapshots;

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
            "GeometryUri": "asset:///Content/Geometry/Box.ogeo.json",
            "OverrideSlots": [ { "$type": "MaterialsSlot", "GeometryUri": "asset:///Content/Geometry/Box.ogeo.json", "MaterialUri": "asset:///Content/Materials/Red.omat.json" } ],
            "TargetedOverrides": [ { "OverrideSlots": [ { "$type": "MaterialsSlot", "GeometryUri": "asset:///Content/Geometry/Box.ogeo.json", "MaterialUri": "asset:///Content/Materials/Red.omat.json" } ] } ] } ] },
            { "Name": "Robot", "Components": [ { "$type": "GeometryComponent", "GeometryUri": "asset:///Content/Geometry/Robot/robot_Body.ogeo" } ] } ],
          "Environment": { "PostProcess": { "AutoExposureMeteringMask": "asset:///Content/Textures/Wood.otex.json" } } }
        """;

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Renaming a material rewrites scene slots (component and targeted) and geometry material references.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingMaterialRewritesEveryReferenceInItsForm()
    {
        using var workspace = CreateContent();
        var plan = await workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Blue.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.Exists("Content/Materials/Red.omat.json").Should().BeFalse();
        _ = workspace.Exists("Content/Materials/Blue.omat.json").Should().BeTrue();
        _ = plan.Referrers.Should().BeEquivalentTo("/Content/Geometry/Box.ogeo.json", "/Content/Scenes/Main.oscene.json");
        var scene = workspace.Read("Content/Scenes/Main.oscene.json");
        _ = scene.Should().NotContain("Red.omat").And.Contain("asset:///Content/Materials/Blue.omat.json");
        _ = workspace.ReadJson("Content/Scenes/Main.oscene.json")["RootNodes"]![0]!["Components"]![0]!["TargetedOverrides"]![0]!["OverrideSlots"]![0]!["MaterialUri"]!.GetValue<string>()
            .Should().Be("asset:///Content/Materials/Blue.omat.json");
        _ = workspace.ReadJson("Content/Geometry/Box.ogeo.json")["lods"]![0]!["submeshes"]![0]!["material_ref"]!.GetValue<string>().Should().Be("/Content/Materials/Blue.omat");
    }

    /// <summary>A texture and its image move together; the descriptor keeps a valid source and a new identity.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingTextureRenamesItsImageAndRewritesIdentityAndReferrers()
    {
        using var workspace = CreateContent();
        _ = await workspace.Service.RelocateAsync(Move("/Content/Textures/Wood.otex.json", "/Content/Textures/Oak.otex.json"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.Exists("Content/Textures/Oak.png").Should().BeTrue();
        _ = workspace.Exists("Content/Textures/Wood.png").Should().BeFalse();
        var texture = workspace.ReadJson("Content/Textures/Oak.otex.json");
        _ = texture["source"]!.GetValue<string>().Should().Be("Oak.png");
        _ = texture["virtual_path"]!.GetValue<string>().Should().Be("/Content/Textures/Oak.otex");
        _ = workspace.Read("Content/Materials/Red.omat.json").Should().Contain("/Content/Textures/Oak.otex");
        _ = workspace.Read("Content/Scenes/Main.oscene.json").Should().Contain("asset:///Content/Textures/Oak.otex.json");
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
        _ = await workspace.Service.RelocateAsync(Move("/Content/Shared", "/Content/Props/Shared"), this.TestContext.CancellationToken).ConfigureAwait(false);

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
        var request = new AssetRelocationRequest { GroupMoves = [new("/Content/SourceMedia/DCC/Robot/robot.gltf", "Hero")] };

        var plan = await workspace.Service.RelocateAsync(request, this.TestContext.CancellationToken).ConfigureAwait(false);

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

        _ = await workspace.Service.RelocateAsync(Move("/Content/Geometry/Vehicles", "/Content/Geometry/Cars"), this.TestContext.CancellationToken).ConfigureAwait(false);

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

        _ = await workspace.Service.RelocateAsync(Move("/Content/SourceMedia/DCC/Robot", "/Content/SourceMedia/Bot"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.ReadSettings("Content/SourceMedia/Bot/robot.gltf.import.json").BundleRoot.Should().Be("Content/SourceMedia/Bot");

        _ = await workspace.Service.RelocateAsync(Move("/Content/SourceMedia/Bot/robot.gltf", "/Content/SourceMedia/Bot/hero.gltf"), this.TestContext.CancellationToken).ConfigureAwait(false);
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
    [DataRow("/Content/Scenes/Main.oscene.json", "/Content/Scenes/Other.oscene.json", DisplayName = "Editor scene")]
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

        Func<Task> work = () => workspace.Service.RelocateAsync(Move(source, target), this.TestContext.CancellationToken);

        _ = await work.Should().ThrowAsync<AssetRelocationException>().ConfigureAwait(false);
        _ = Snapshot(workspace.Root).Should().BeEquivalentTo(before);
    }

    /// <summary>An output group cannot overlap another import's group.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task GroupOverlappingAnotherImportIsRejected()
    {
        using var workspace = CreateContent();
        _ = workspace.WriteModel("Content/SourceMedia/DCC/Robot", "robot.gltf", "Robot");
        _ = workspace.WriteModel("Content/SourceMedia/DCC/Car", "car.gltf", "Car");
        var request = new AssetRelocationRequest { GroupMoves = [new("/Content/SourceMedia/DCC/Robot/robot.gltf", "Car/Robot")] };

        Func<Task> work = () => workspace.Service.RelocateAsync(request, this.TestContext.CancellationToken);

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

        Func<Task> work = () => workspace.Service.RelocateAsync(Move("/Content/Textures/Wood.otex.json", "/Content/Textures/Oak.otex.json"), this.TestContext.CancellationToken);

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
        var path = workspace.PathOf("Content/Scenes/Main.oscene.json");
        using var registration = workspace.Documents.Register(path, _ => Task.FromResult<CookDocumentReadLease?>(null));
        registration.UpdateState(new(Guid.NewGuid(), path, "Main", Revision: 2, SavedRevision: 1, IsDirty: true, SavedContentHash: string.Empty));

        Func<Task> work = () => workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Blue.omat.json"), this.TestContext.CancellationToken);

        _ = await work.Should().ThrowAsync<AssetRelocationException>().WithMessage("*Main*").ConfigureAwait(false);
        _ = workspace.Exists("Content/Materials/Red.omat.json").Should().BeTrue();
    }

    /// <summary>Copies get unique names and their own identity; referrers keep pointing at the original.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CopyCreatesUniqueCopiesWithTheirOwnIdentity()
    {
        using var workspace = CreateContent();
        var copies = await workspace.Service.CopyAsync(["/Content/Textures/Wood.otex.json"], "/Content/Textures", this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = copies.Should().ContainSingle().Which.Should().Be("/Content/Textures/Wood (2).otex.json");
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
            ["/Content/Textures/Wood.otex.json", "/Content/SourceMedia/DCC/Robot/robot.gltf"],
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

    /// <summary>Deleting a texture keeps an image that another texture still uses.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task DeleteKeepsImageSharedWithAnotherTexture()
    {
        using var workspace = CreateContent();
        _ = workspace.Write("Content/Textures/Oak.otex.json", """{ "name": "Oak", "virtual_path": "/Content/Textures/Oak.otex", "source": "Wood.png" }""");
        var recycled = new List<string>();

        await workspace.Service.DeleteAsync(
            ["/Content/Textures/Wood.otex.json"],
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

        var plan = await workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Red.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);

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
        _ = (await workspace.Service.FindReferrersAsync("/Content/Materials/Red.omat.json", this.TestContext.CancellationToken).ConfigureAwait(false))
            .Should().BeEquivalentTo("/Content/Geometry/Box.ogeo.json", "/Content/Scenes/Main.oscene.json");
        _ = (await workspace.Service.FindReferrersAsync("/Content/Textures", this.TestContext.CancellationToken).ConfigureAwait(false))
            .Should().BeEquivalentTo("/Content/Materials/Red.omat.json", "/Content/Scenes/Main.oscene.json");
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

    private static AssetRelocationRequest Move(string source, string target) => new() { Moves = [new(source, target)] };

    private static Dictionary<string, string> Snapshot(string root)
        => Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)
            .Where(path => !Path.GetRelativePath(root, path).StartsWith(".build", StringComparison.Ordinal))
            .ToDictionary(path => Path.GetRelativePath(root, path), File.ReadAllText, StringComparer.Ordinal);
}
