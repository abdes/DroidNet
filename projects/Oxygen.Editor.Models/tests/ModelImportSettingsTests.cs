// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using System.Text.Json;
using AwesomeAssertions;

namespace Oxygen.Editor.Models.Tests;

/// <summary>
/// A model import record holds the engine's schema: the editor writes what the ImportTool reads, relocation rewrites
/// the source alone, and an import changes only what it establishes.
/// </summary>
[TestClass]
public sealed class ModelImportSettingsTests
{
    private const string ProjectRoot = @"C:\Project";
    private static readonly string Source = Path.Combine(ProjectRoot, "Content", "Car", "Car.glb");

    private readonly Guid identity = Guid.CreateVersion7();

    private string RecordPath => ModelImportSettings.PathFor(ProjectRoot, this.identity);

    [TestMethod]
    public void ARecordIsNamedByItsIdentityAndKeepsItsSourceRelativeToItself()
    {
        var record = this.Create();

        _ = this.RecordPath.Should().Be(Path.Combine(ProjectRoot, "imports", $"{this.identity:D}.import.json"));
        _ = record.SourceIdentity.Should().Be(this.identity);
        _ = record.Source.Should().Be("../Content/Car/Car.glb");
        _ = record.ResolveSource(this.RecordPath).Should().Be(Source);
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
    public void ARecordNotNamedByItsIdentityOrOffSchemaIsRefused()
    {
        var bytes = this.Create().ToBytes();

        var renamed = () => ModelImportSettings.Parse(Path.Combine(ProjectRoot, "imports", "Car.import.json"), bytes);
        var invalid = () => ModelImportSettings.Parse(Encoding.UTF8.GetBytes("""{ "schema_version": 1 }"""));

        _ = renamed.Should().Throw<InvalidDataException>();
        _ = invalid.Should().Throw<InvalidDataException>();
    }

    [TestMethod]
    public void RelocationRewritesTheSourceAlone()
    {
        var record = this.Create();
        var moved = Path.Combine(ProjectRoot, "Content", "Vehicles", "Car.glb");

        var relocated = record.WithSource(this.RecordPath, moved);

        _ = relocated.Source.Should().Be("../Content/Vehicles/Car.glb");
        _ = relocated.DiffersOnlyByImport(record).Should().BeFalse();
        _ = (relocated with { Source = record.Source }).ToBytes().Should().Equal(record.ToBytes());
    }

    [TestMethod]
    public void ASourceOutsideTheProjectIsKeptAbsolute()
    {
        const string outside = @"D:\Assets\Car.glb";

        var record = this.Create().WithSource(this.RecordPath, outside);

        _ = record.Source.Should().Be("D:/Assets/Car.glb");
        _ = record.ResolveSource(this.RecordPath).Should().Be(outside);
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
        return ModelImportSettings.Create(this.RecordPath, Source, "/Content/Car/Car.glb", settings.RootElement);
    }
}
