// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Models.Tests;

/// <summary>
/// A model is imported when a record names it: the index finds a project's records by source and by virtual path, and
/// a damaged record is listed without hiding the others.
/// </summary>
[TestClass]
public sealed class ModelImportRecordsTests
{
    private const string ProjectRoot = @"C:\Project";

    private readonly MockFileSystem fileSystem = new();

    [TestMethod]
    public void TheIndexFindsARecordBySource()
    {
        var source = Path.Combine(ProjectRoot, "Content", "Car.glb");
        var path = this.Save(source, "/Content/Car.glb");

        var records = new ModelImportRecords(this.fileSystem, ProjectRoot);

        _ = records.IsImported(source).Should().BeTrue();
        _ = records.IsImported(Path.Combine(ProjectRoot, "content", "car.glb")).Should().BeFalse("a path that differs only in case names another asset");
        _ = records.IsImported(Path.Combine(ProjectRoot, "Content", "Other.glb")).Should().BeFalse();
        _ = records.TryFindBySource(source, out var found).Should().BeTrue();
        _ = found!.RecordPath.Should().Be(path);
        _ = found.SourcePath.Should().Be(source);
    }

    [TestMethod]
    public void AProjectWithoutRecordsImportsNothing()
    {
        var records = new ModelImportRecords(this.fileSystem, ProjectRoot);

        _ = records.All.Should().BeEmpty();
        _ = records.Problems.Should().BeEmpty();
    }

    [TestMethod]
    public void ADamagedRecordIsListedWithoutHidingTheOthers()
    {
        var source = Path.Combine(ProjectRoot, "Content", "Car.glb");
        _ = this.Save(source, "/Content/Car.glb");
        var damaged = ModelImportSettings.PathFor(ProjectRoot, "/Content/Broken.glb");
        _ = this.fileSystem.Directory.CreateDirectory(Path.GetDirectoryName(damaged)!);
        this.fileSystem.File.WriteAllText(damaged, "{");

        var records = new ModelImportRecords(this.fileSystem, ProjectRoot);

        _ = records.All.Should().ContainSingle();
        _ = records.Problems.Should().ContainSingle();
    }

    [TestMethod]
    public void TheIndexListsTheRecordsOfEveryFolderBelowImports()
    {
        var car = this.Save(Path.Combine(ProjectRoot, "Content", "Car.glb"), "/Content/Car.glb");
        var ship = this.Save(Path.Combine(ProjectRoot, "Content", "Models", "Ships", "Ship.glb"), "/Content/Models/Ships/Ship.glb");
        var other = this.Save(Path.Combine(ProjectRoot, "Props", "Deep", "Er", "Crate.glb"), "/Props/Deep/Er/Crate.glb");

        var records = new ModelImportRecords(this.fileSystem, ProjectRoot);

        _ = records.Problems.Should().BeEmpty();
        _ = records.All.Select(static record => record.RecordPath).Should().BeEquivalentTo([car, ship, other]);
        _ = ship.Should().Be(Path.Combine(ProjectRoot, "Imports", "Content", "Models", "Ships", "Ship.glb.import.json"));
        _ = records.TryFindBySource(Path.Combine(ProjectRoot, "Props", "Deep", "Er", "Crate.glb"), out var found).Should().BeTrue();
        _ = found!.RecordPath.Should().Be(other);
    }

    [TestMethod]
    public void AMisplacedRecordIsListedAsAProblemWithoutHidingTheOthers()
    {
        var source = Path.Combine(ProjectRoot, "Content", "Car.glb");
        _ = this.Save(source, "/Content/Car.glb");
        var place = ModelImportSettings.PathFor(ProjectRoot, "/Content/Truck.glb");
        using var settings = JsonDocument.Parse("{}");
        var bytes = ModelImportSettings.Create(place, Path.Combine(ProjectRoot, "Content", "Truck.glb"), "/Content/Truck.glb", settings.RootElement).ToBytes();
        var misplaced = Path.Combine(ProjectRoot, "Imports", "Content", "Models", "Truck.glb.import.json");
        _ = this.fileSystem.Directory.CreateDirectory(Path.GetDirectoryName(misplaced)!);
        this.fileSystem.File.WriteAllBytes(misplaced, bytes);

        var records = new ModelImportRecords(this.fileSystem, ProjectRoot);

        _ = records.All.Should().ContainSingle().Which.Settings.VirtualPath.Should().Be("/Content/Car.glb");
        _ = records.Problems.Should().ContainSingle().Which.RecordPath.Should().Be(misplaced);
    }

    private string Save(string source, string virtualPath)
    {
        var path = ModelImportSettings.PathFor(ProjectRoot, virtualPath);
        using var settings = JsonDocument.Parse("{}");
        _ = this.fileSystem.Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        this.fileSystem.File.WriteAllBytes(path, ModelImportSettings.Create(path, source, virtualPath, settings.RootElement).ToBytes());
        return path;
    }
}
