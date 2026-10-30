// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Models.Tests;

/// <summary>
/// A model is imported when a record names it: the index finds a project's records by source and by virtual path, each
/// source resolved through the project's mount table, and a record that cannot be read, or whose source's mount is not
/// bound, is listed without hiding the others.
/// </summary>
[TestClass]
public sealed class ModelImportRecordsTests
{
    private const string ProjectRoot = @"C:\Project";

    private readonly MockFileSystem fileSystem = new();

    [TestMethod]
    public void TheIndexFindsARecordBySource()
    {
        var project = this.CreateProject();
        var path = this.Save("/Content/Car.glb", "/Content/Car.glb");
        var source = Path.Combine(ProjectRoot, "Content", "Car.glb");

        var records = new ModelImportRecords(project);

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
        var records = new ModelImportRecords(this.CreateProject());

        _ = records.All.Should().BeEmpty();
        _ = records.Problems.Should().BeEmpty();
    }

    [TestMethod]
    public void ADamagedRecordIsListedWithoutHidingTheOthers()
    {
        var project = this.CreateProject();
        _ = this.Save("/Content/Car.glb", "/Content/Car.glb");
        var damaged = ModelImportSettings.PathFor(ProjectRoot, "/Content/Broken.glb");
        _ = this.fileSystem.Directory.CreateDirectory(Path.GetDirectoryName(damaged)!);
        this.fileSystem.File.WriteAllText(damaged, "{");

        var records = new ModelImportRecords(project);

        _ = records.All.Should().ContainSingle();
        _ = records.Problems.Should().ContainSingle().Which.UnboundSource.Should().BeNull();
    }

    [TestMethod]
    public void TheIndexListsTheRecordsOfEveryFolderBelowImports()
    {
        var project = this.CreateProject();
        var car = this.Save("/Content/Car.glb", "/Content/Car.glb");
        var ship = this.Save("/Content/Models/Ships/Ship.glb", "/Content/Models/Ships/Ship.glb");
        var other = this.Save("/Props/Deep/Er/Crate.glb", "/Props/Deep/Er/Crate.glb");

        var records = new ModelImportRecords(project);

        _ = records.Problems.Should().BeEmpty();
        _ = records.All.Select(static record => record.RecordPath).Should().BeEquivalentTo([car, ship, other]);
        _ = ship.Should().Be(Path.Combine(ProjectRoot, "Imports", "Content", "Models", "Ships", "Ship.glb.import.json"));
        _ = records.TryFindBySource(Path.Combine(ProjectRoot, "Props", "Deep", "Er", "Crate.glb"), out var found).Should().BeTrue();
        _ = found!.RecordPath.Should().Be(other);
    }

    [TestMethod]
    public void AMisplacedRecordIsListedAsAProblemWithoutHidingTheOthers()
    {
        var project = this.CreateProject();
        _ = this.Save("/Content/Car.glb", "/Content/Car.glb");
        var place = ModelImportSettings.PathFor(ProjectRoot, "/Content/Truck.glb");
        using var settings = JsonDocument.Parse("{}");
        var bytes = ModelImportSettings.Create(place, "/Content/Truck.glb", "/Content/Truck.glb", settings.RootElement).ToBytes();
        var misplaced = Path.Combine(ProjectRoot, "Imports", "Content", "Models", "Truck.glb.import.json");
        _ = this.fileSystem.Directory.CreateDirectory(Path.GetDirectoryName(misplaced)!);
        this.fileSystem.File.WriteAllBytes(misplaced, bytes);

        var records = new ModelImportRecords(project);

        _ = records.All.Should().ContainSingle().Which.Settings.VirtualPath.Should().Be("/Content/Car.glb");
        _ = records.Problems.Should().ContainSingle().Which.RecordPath.Should().Be(misplaced);
    }

    [TestMethod]
    public void ARecordInAMountOutsideTheProjectResolvesItsSourceThroughTheMountsFile()
    {
        _ = this.fileSystem.Directory.CreateDirectory(@"D:\Studio\SharedContent");
        _ = this.fileSystem.Directory.CreateDirectory(ProjectRoot);
        this.fileSystem.File.WriteAllText(Path.Combine(ProjectRoot, ProjectMounts.FileName), """{"version":1,"mounts":{"Studio":"D:/Studio/SharedContent"}}""");
        var project = TestProject.Create(this.fileSystem, ProjectRoot, [new ProjectMount("Content", Path: "Content"), new ProjectMount("Studio")]).Context;
        _ = this.Save("/Studio/Vehicles/Car.glb", "/Studio/Vehicles/Car.glb");

        var records = new ModelImportRecords(project);

        _ = records.All.Should().ContainSingle().Which.SourcePath.Should().Be(@"D:\Studio\SharedContent\Vehicles\Car.glb");
        _ = records.IsImported(@"D:\Studio\SharedContent\Vehicles\Car.glb").Should().BeTrue();
    }

    [TestMethod]
    public void ARecordWhoseSourceMountIsNotBound_IsAProblemThatNamesItsSource()
    {
        var project = TestProject.Create(this.fileSystem, ProjectRoot, [new ProjectMount("Content", Path: "Content"), new ProjectMount("Studio")]).Context;
        _ = project.Mounts.Single(static mount => mount.Name == "Studio").State.Should().Be(MountState.Unbound);
        var path = this.Save("/Studio/Vehicles/Car.glb", "/Studio/Vehicles/Car.glb");

        var records = new ModelImportRecords(project);

        _ = records.All.Should().BeEmpty();
        var problem = records.Problems.Should().ContainSingle().Subject;
        _ = problem.RecordPath.Should().Be(path);
        _ = problem.UnboundSource.Should().Be("/Studio/Vehicles/Car.glb");
    }

    [TestMethod]
    public void ARecordThatNamesItsSourceRelativeToItsFolder_IsAProblem()
    {
        var project = this.CreateProject();
        var path = this.Save("/Content/Car.glb", "/Content/Car.glb");
        var text = this.fileSystem.File.ReadAllText(path).Replace("\"source\": \"/Content/Car.glb\"", "\"source\": \"../../Content/Car.glb\"", StringComparison.Ordinal);
        this.fileSystem.File.WriteAllText(path, text);

        var records = new ModelImportRecords(project);

        _ = records.All.Should().BeEmpty();
        _ = records.Problems.Should().ContainSingle().Which.UnboundSource.Should().BeNull("the record is not readable: its source is not a mount path");
    }

    private ProjectContext CreateProject()
        => TestProject.Create(this.fileSystem, ProjectRoot, ["Content", "Props"]).Context;

    private string Save(string source, string virtualPath)
    {
        var path = ModelImportSettings.PathFor(ProjectRoot, virtualPath);
        using var settings = JsonDocument.Parse("{}");
        _ = this.fileSystem.Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        this.fileSystem.File.WriteAllBytes(path, ModelImportSettings.Create(path, source, virtualPath, settings.RootElement).ToBytes());
        return path;
    }
}
