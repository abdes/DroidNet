// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Models.Tests;

/// <summary>
/// A model import record holds the engine's schema: the editor writes what the ImportTool reads, the source is a mount
/// path resolved through the project's mount table, relocation rewrites the source alone, and an import changes only
/// what it establishes.
/// </summary>
[TestClass]
public sealed class ModelImportSettingsTests
{
    private const string ProjectRoot = @"C:\Project";
    private const string Source = "/Content/Car/Car.glb";
    private const string VirtualPath = "/Content/Car/Car.glb";

    private readonly Guid identity = Guid.CreateVersion7();

    private string RecordPath => ModelImportSettings.PathFor(ProjectRoot, VirtualPath);

    [TestMethod]
    public void ARecordLivesAtItsVirtualPathAndNamesItsSourceByMountPath()
    {
        var record = this.Create();
        var project = TestProject.Create(new MockFileSystem(), ProjectRoot).Context;

        _ = this.RecordPath.Should().Be(Path.Combine(ProjectRoot, "Imports", "Content", "Car", "Car.glb.import.json"));
        _ = ModelImportSettings.RelativePathFor(VirtualPath).Should().Be("Imports/Content/Car/Car.glb.import.json");
        _ = record.SourceIdentity.Should().Be(this.identity);
        _ = record.Source.Should().Be("/Content/Car/Car.glb");
        _ = record.ResolveSource(project)!.FullPath.Should().Be(Path.Combine(ProjectRoot, "Content", "Car", "Car.glb"));
        _ = record.SourceFingerprint.Should().BeNull("a record has no fingerprint before its first import");
    }

    [TestMethod]
    public void ARecordRoundTripsThroughTheEngineSchema()
    {
        var record = this.Create() with
        {
            NodeContinuity = [new("Body/Door_Front_L", "Body/Door_FL")],
            PublishedNodes = [new("Body", Guid.NewGuid()), new("Body/Door_Front_L", Guid.NewGuid())],
            NewNodes = [new("Body/Door_Front_L", Guid.NewGuid())],
            VanishedNodes = [new("Body/Door_FL", Guid.NewGuid())],
            SourceFingerprint = new(10, new string('a', 64), [new("Car.bin", 4, new string('b', 64))]),
        };

        var bytes = record.ToBytes();
        var read = ModelImportSettings.Parse(this.RecordPath, bytes);

        _ = read.ToBytes().Should().Equal(bytes);
        _ = read.NodeContinuity.Should().Equal(record.NodeContinuity);
        _ = read.PublishedNodes.Should().Equal(record.PublishedNodes);
        _ = read.NewNodes.Should().Equal(record.NewNodes);
        _ = read.VanishedNodes.Should().Equal(record.VanishedNodes);
        _ = read.SourceFingerprint!.Files.Should().Equal(record.SourceFingerprint.Files);
        using var document = JsonDocument.Parse(bytes);
        _ = document.RootElement.GetProperty("schema_version").GetInt32().Should().Be(ModelImportSettings.CurrentSchemaVersion);
        _ = document.RootElement.GetProperty("settings").GetProperty("unit_policy").GetString().Should().Be("normalize");
    }

    [TestMethod]
    public void ARecordNotAtItsVirtualPathOrOffSchemaIsRefused()
    {
        var bytes = this.Create().ToBytes();

        var misplaced = () => ModelImportSettings.Parse(Path.Combine(ProjectRoot, "Imports", "Car.import.json"), bytes);
        var named = () => ModelImportSettings.Parse(Path.Combine(ProjectRoot, "Imports", "Content", "Car", $"{this.identity:D}.import.json"), bytes);
        var elsewhere = () => ModelImportSettings.Parse(Path.Combine(ProjectRoot, "Content", "Car", "Car.glb.import.json"), bytes);
        var invalid = () => ModelImportSettings.Parse(Encoding.UTF8.GetBytes("""{ "schema_version": 1 }"""));

        _ = misplaced.Should().Throw<InvalidDataException>();
        _ = named.Should().Throw<InvalidDataException>("the file name no longer carries the source identity");
        _ = elsewhere.Should().Throw<InvalidDataException>("a record lives under Imports");
        _ = invalid.Should().Throw<InvalidDataException>();
        _ = ModelImportSettings.Parse(this.RecordPath, bytes).VirtualPath.Should().Be(VirtualPath);
    }

    [TestMethod]
    public void ANewRecordMustBeCreatedAtItsVirtualPathWithAUuidV7Identity()
    {
        using var settings = JsonDocument.Parse("{}");

        var misplaced = () => ModelImportSettings.Create(Path.Combine(ProjectRoot, "Imports", "Car.import.json"), Source, VirtualPath, settings.RootElement);
        var notV7 = () => ModelImportSettings.Create(this.RecordPath, Source, VirtualPath, settings.RootElement, Guid.NewGuid());
        var relative = () => ModelImportSettings.PathFor(ProjectRoot, "Content/Car/Car.glb");

        _ = misplaced.Should().Throw<ArgumentException>();
        _ = notV7.Should().Throw<ArgumentException>();
        _ = relative.Should().Throw<ArgumentException>();
        _ = ModelImportSettings.Create(this.RecordPath, Source, VirtualPath, settings.RootElement).SourceIdentity.Version.Should().Be(7);
    }

    [TestMethod]
    public void ARecordGivenAnotherVirtualPathBelongsAtTheNewPlace()
    {
        var record = this.Create();
        var path = record.VirtualPath.Replace("Car.glb", "Truck.glb", StringComparison.Ordinal);
        var moved = record with { VirtualPath = path };

        var newPlace = ModelImportSettings.PathFor(ProjectRoot, path);

        _ = newPlace.Should().Be(Path.Combine(ProjectRoot, "Imports", "Content", "Car", "Truck.glb.import.json"));
        _ = ModelImportSettings.ProjectRootOf(newPlace, moved.VirtualPath).Should().Be(ProjectRoot);
        _ = ModelImportSettings.ProjectRootOf(this.RecordPath, moved.VirtualPath).Should().BeNull("the old place is no longer its place");
        var parse = () => ModelImportSettings.Parse(this.RecordPath, moved.ToBytes());
        _ = parse.Should().Throw<InvalidDataException>();
        _ = ModelImportSettings.Parse(newPlace, moved.ToBytes()).SourceIdentity.Should().Be(record.SourceIdentity);
    }

    [TestMethod]
    public void RelocationRewritesTheSourceAlone()
    {
        var record = this.Create();

        var relocated = record.WithSource("/Content/Vehicles/Car.glb");

        _ = relocated.Source.Should().Be("/Content/Vehicles/Car.glb");
        _ = relocated.DiffersOnlyByImport(record).Should().BeFalse();
        _ = (relocated with { Source = record.Source }).ToBytes().Should().Equal(record.ToBytes());
    }

    [TestMethod]
    public void ASourceInAMountOutsideTheProjectResolvesThroughItsBinding()
    {
        var fileSystem = new MockFileSystem();
        _ = fileSystem.Directory.CreateDirectory(@"D:\Assets");
        _ = fileSystem.Directory.CreateDirectory(ProjectRoot);
        fileSystem.File.WriteAllText(Path.Combine(ProjectRoot, ProjectMounts.FileName), """{"schema_version":2,"mounts":{"Studio":"D:/Assets"}}""");
        var project = TestProject.Create(fileSystem, ProjectRoot, [new ProjectMount("Content", Path: "Content"), new ProjectMount("Studio")]).Context;

        var record = this.Create().WithSource("/Studio/Car.glb");

        _ = record.Source.Should().Be("/Studio/Car.glb");
        _ = record.ResolveSource(project)!.FullPath.Should().Be(@"D:\Assets\Car.glb");
        _ = record.WithSource("/Vendor/Car.glb").ResolveSource(project).Should().BeNull("the project has no Vendor mount");
    }

    [TestMethod]
    [DataRow("../../../Content/Car/Car.glb")]
    [DataRow("D:/Assets/Car.glb")]
    [DataRow("/Content/../Car.glb")]
    public void ASourceThatIsNotAMountPathIsRefused(string source)
    {
        var record = this.Create();

        var rebind = () => record.WithSource(source);
        var create = () => ModelImportSettings.Create(this.RecordPath, source, VirtualPath, record.Settings);
        var parse = () => ModelImportSettings.Parse(this.RecordPath, Encoding.UTF8.GetBytes(Encoding.UTF8.GetString(record.ToBytes()).Replace(
            "\"source\": \"/Content/Car/Car.glb\"", "\"source\": \"" + source + "\"", StringComparison.Ordinal)));

        _ = rebind.Should().Throw<ArgumentException>();
        _ = create.Should().Throw<ArgumentException>();
        _ = parse.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public void AnImportUpdateChangesOnlyWhatTheImportEstablished()
    {
        var record = this.Create() with { NodeContinuity = [new("B", "A")] };
        using var update = JsonDocument.Parse("""
            {
              "surface_provenance": { "schema_version": 2, "geometries": [] },
              "published_nodes": [ { "anchor": "Body", "node_id": "01990000-0000-7000-8000-000000000009" } ],
              "new_nodes": [ { "anchor": "Body", "node_id": "01990000-0000-7000-8000-000000000009" } ],
              "vanished_nodes": [ { "anchor": "Body/Door_FL", "node_id": "01990000-0000-7000-8000-00000000000a" } ],
              "source_fingerprint": { "size": 3, "sha256": "0000000000000000000000000000000000000000000000000000000000000000", "files": [] }
            }
            """);

        var updated = record.WithImportUpdate(update.RootElement);

        _ = updated.PublishedNodes.Should().ContainSingle().Which.Anchor.Should().Be("Body");
        _ = updated.NewNodes.Should().ContainSingle().Which.Anchor.Should().Be("Body");
        _ = updated.VanishedNodes.Should().ContainSingle().Which.Anchor.Should().Be("Body/Door_FL");
        _ = updated.SourceFingerprint!.Size.Should().Be(3);
        _ = updated.NodeContinuity.Should().Equal(record.NodeContinuity);
        _ = updated.DiffersOnlyByImport(record).Should().BeTrue();
        _ = (updated with { VirtualPath = "/Content/Other.glb" }).DiffersOnlyByImport(record).Should().BeFalse();
    }

    [TestMethod]
    public void AContinuityEntry_IsAddedAlone_AndAnAnchorContinuesOneNode()
    {
        var record = this.Create();

        var continued = record.WithContinuity("Body/Door_Front_L", "Body/Door_FL");

        _ = continued.NodeContinuity.Should().Equal([new ModelNodeContinuity("Body/Door_Front_L", "Body/Door_FL")]);
        _ = (continued with { NodeContinuity = record.NodeContinuity }).ToBytes().Should().Equal(record.ToBytes(), "nothing else changes");
        _ = ModelImportSettings.Parse(continued.ToBytes()).NodeContinuity.Should().Equal(continued.NodeContinuity);
        var again = () => continued.WithContinuity("Body/Door_Front_L", "Body/Door_RL");
        var itself = () => record.WithContinuity("Body", "Body");
        _ = again.Should().Throw<ArgumentException>();
        _ = itself.Should().Throw<ArgumentException>();
    }

    private ModelImportSettings Create()
    {
        using var settings = JsonDocument.Parse("""{ "unit_policy": "normalize", "bake_transforms": false }""");
        return ModelImportSettings.Create(this.RecordPath, Source, VirtualPath, settings.RootElement, this.identity);
    }
}
