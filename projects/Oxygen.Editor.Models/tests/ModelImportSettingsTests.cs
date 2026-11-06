// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Models.Tests;

/// <summary>
/// A model import record holds the engine's schema: the editor writes what the ImportTool reads, the source is a file
/// reference from the record resolved through the project's mount table, the record's identity is where it lives and never
/// in the file, relocation rewrites the source alone, and an import changes only what it establishes.
/// </summary>
[TestClass]
public sealed class ModelImportSettingsTests
{
    private const string ProjectRoot = @"C:\Project";
    private const string Source = "Car.glb";

    private static readonly AssetPath Model = AssetPath.Parse("/Content/Car/Car.glb");

    private readonly Guid identity = Guid.CreateVersion7();

    [TestMethod]
    public void ARecordNamesItsSourceRelativelyAndResolvesItThroughItsIdentity()
    {
        var record = this.Create();
        var project = TestProject.Create(new MockFileSystem(), ProjectRoot).Context;

        _ = record.Identity.Should().Be(Model);
        _ = record.SourceIdentity.Should().Be(this.identity);
        _ = record.Source.Should().Be("Car.glb");
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
        var read = ModelImportSettings.Parse(bytes, Model);

        _ = read.ToBytes().Should().Equal(bytes);
        _ = read.Identity.Should().Be(Model);
        _ = read.NodeContinuity.Should().Equal(record.NodeContinuity);
        _ = read.PublishedNodes.Should().Equal(record.PublishedNodes);
        _ = read.NewNodes.Should().Equal(record.NewNodes);
        _ = read.VanishedNodes.Should().Equal(record.VanishedNodes);
        _ = read.SourceFingerprint!.Files.Should().Equal(record.SourceFingerprint.Files);
        using var document = JsonDocument.Parse(bytes);
        _ = document.RootElement.GetProperty("schema_version").GetInt32().Should().Be(ModelImportSettings.CurrentSchemaVersion);
        _ = document.RootElement.GetProperty("settings").GetProperty("unit_policy").GetString().Should().Be("normalize");
        _ = document.RootElement.TryGetProperty("virtual_path", out _).Should().BeFalse("a record's identity is where it lives");
    }

    [TestMethod]
    public void ARecordOffSchemaOrHoldingItsIdentityIsRefused()
    {
        var bytes = this.Create().ToBytes();

        var invalid = () => ModelImportSettings.Parse(Encoding.UTF8.GetBytes($$"""{ "schema_version": {{ModelImportSettings.CurrentSchemaVersion}} }"""), Model);
        var holdingItsIdentity = () => ModelImportSettings.Parse(Encoding.UTF8.GetBytes(Encoding.UTF8.GetString(bytes).Replace(
            "\"source\": \"Car.glb\"", "\"source\": \"Car.glb\", \"virtual_path\": \"/Content/Car/Car.glb\"", StringComparison.Ordinal)), Model);

        _ = invalid.Should().Throw<InvalidDataException>();
        _ = holdingItsIdentity.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public void ANewRecordHasAUuidV7Identity()
    {
        using var settings = JsonDocument.Parse("{}");

        var notV7 = () => ModelImportSettings.Create(Model, Source, settings.RootElement, Guid.NewGuid());

        _ = notV7.Should().Throw<ArgumentException>();
        _ = ModelImportSettings.Create(Model, Source, settings.RootElement).SourceIdentity.Version.Should().Be(7);
    }

    [TestMethod]
    public void RelocationRewritesTheSourceAlone()
    {
        var record = this.Create();

        var relocated = record.WithSource("../Vehicles/Car.glb");

        _ = relocated.Source.Should().Be("../Vehicles/Car.glb");
        _ = relocated.DiffersOnlyByImport(record).Should().BeFalse();
        _ = (relocated with { Source = record.Source }).ToBytes().Should().Equal(record.ToBytes());
    }

    [TestMethod]
    public void ASourceInAMountOutsideTheProjectResolvesThroughItsBinding()
    {
        var fileSystem = new MockFileSystem();
        _ = fileSystem.Directory.CreateDirectory(@"D:\Assets");
        _ = fileSystem.Directory.CreateDirectory(ProjectRoot);
        fileSystem.File.WriteAllText(Path.Combine(ProjectRoot, ProjectMounts.FileName), $$$"""{"schema_version":{{{ProjectMounts.SchemaVersion}}},"mounts":{"Studio":"D:/Assets"}}""");
        var project = TestProject.Create(fileSystem, ProjectRoot, [new ProjectMount("Content", Path: "Content"), new ProjectMount("Studio")]).Context;

        var record = this.Create().WithSource("/Studio/Car.glb");

        _ = record.Source.Should().Be("/Studio/Car.glb");
        _ = record.ResolveSource(project)!.FullPath.Should().Be(@"D:\Assets\Car.glb");
        _ = record.WithSource("/Vendor/Car.glb").ResolveSource(project).Should().BeNull("the project has no Vendor mount");
    }

    [TestMethod]
    [DataRow("../../../Content/Car/Car.glb")]
    [DataRow("D:/Assets/Car.glb")]
    [DataRow("/Content/Car/Car.glb")]
    [DataRow("/Other/../Car.glb")]
    public void ASourceThatIsNoFileReferenceFromTheRecordIsRefused(string source)
    {
        var record = this.Create();

        var rebind = () => record.WithSource(source);
        var create = () => ModelImportSettings.Create(Model, source, record.Settings);
        var parse = () => ModelImportSettings.Parse(
            Encoding.UTF8.GetBytes(Encoding.UTF8.GetString(record.ToBytes()).Replace("\"source\": \"Car.glb\"", "\"source\": \"" + source + "\"", StringComparison.Ordinal)),
            Model);

        _ = rebind.Should().Throw<ArgumentException>();
        _ = create.Should().Throw<ArgumentException>();
        _ = parse.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public void AnImportUpdateChangesOnlyWhatTheImportEstablished()
    {
        var record = this.Create() with { NodeContinuity = [new("B", "A")] };
        using var update = JsonDocument.Parse($$$"""
            {
              "surface_provenance": { "schema_version": {{{EngineSchemaVersions.SurfaceProvenance}}}, "geometries": [] },
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
        _ = updated.WithSource("Other.glb").DiffersOnlyByImport(record).Should().BeFalse();
    }

    [TestMethod]
    public void AContinuityEntry_IsAddedAlone_AndAnAnchorContinuesOneNode()
    {
        var record = this.Create();

        var continued = record.WithContinuity("Body/Door_Front_L", "Body/Door_FL");

        _ = continued.NodeContinuity.Should().Equal([new ModelNodeContinuity("Body/Door_Front_L", "Body/Door_FL")]);
        _ = (continued with { NodeContinuity = record.NodeContinuity }).ToBytes().Should().Equal(record.ToBytes(), "nothing else changes");
        _ = ModelImportSettings.Parse(continued.ToBytes(), Model).NodeContinuity.Should().Equal(continued.NodeContinuity);
        var again = () => continued.WithContinuity("Body/Door_Front_L", "Body/Door_RL");
        var itself = () => record.WithContinuity("Body", "Body");
        _ = again.Should().Throw<ArgumentException>();
        _ = itself.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void ANodeRenamedBack_ReplacesTheEntryItWouldCloseACircleWith()
    {
        var renamed = this.Create().WithContinuity("Body/B", "Body/A");

        var back = renamed.WithContinuity("Body/A", "Body/B");

        _ = back.NodeContinuity.Should().Equal([new ModelNodeContinuity("Body/A", "Body/B")], "two entries that continue each other block the model");
    }

    [TestMethod]
    public void AnEntryThatClosesALongerChain_ReplacesTheEntryItCloses()
    {
        var chain = this.Create().WithContinuity("B", "A").WithContinuity("C", "B").WithContinuity("D", "C");

        var closed = chain.WithContinuity("A", "D");

        _ = closed.NodeContinuity.Should().Equal(
        [
            new ModelNodeContinuity("C", "B"),
            new ModelNodeContinuity("D", "C"),
            new ModelNodeContinuity("A", "D"),
        ]);
    }

    [TestMethod]
    public void AnEntryThatClosesNoCircle_LeavesTheOthers()
    {
        var chain = this.Create().WithContinuity("B", "A");

        var other = chain.WithContinuity("D", "C");

        _ = other.NodeContinuity.Should().Equal([new ModelNodeContinuity("B", "A"), new ModelNodeContinuity("D", "C")]);
    }

    private ModelImportSettings Create()
    {
        using var settings = JsonDocument.Parse("""{ "unit_policy": "normalize", "bake_transforms": false }""");
        return ModelImportSettings.Create(Model, Source, settings.RootElement, this.identity);
    }
}
