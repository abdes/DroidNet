// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Models.Testing;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Models.Tests;

/// <summary>
/// A model is imported when it has a record at its identity: the index lists the records of the bound source mounts, finds
/// them by identity and by the source they name, each source resolved through the project's mount table, and a record that
/// cannot be read, or whose source's mount is not bound, is listed without hiding the others.
/// </summary>
[TestClass]
public sealed class ModelImportRecordsTests
{
    private const string ProjectRoot = @"C:\Project";

    private readonly MockFileSystem fileSystem = new();

    [TestMethod]
    public void TheIndexFindsARecordByIdentity()
    {
        var project = this.CreateProject();
        var path = this.Save(project, "/Content/Car.glb", "/Content/Car.glb");

        var records = new ModelImportRecords(project);

        _ = records.TryFind(AssetPath.Parse("/Content/Car.glb"), out var found).Should().BeTrue();
        _ = found!.RecordPath.Should().Be(path);
        _ = found.Identity.Should().Be(AssetPath.Parse("/Content/Car.glb"));
        _ = found.SourcePath.Should().Be(Path.Combine(ProjectRoot, "Content", "Car.glb"));
        _ = records.TryFind(AssetPath.Parse("/Content/Other.glb"), out _).Should().BeFalse();
    }

    [TestMethod]
    public void ASourceHasAnyNumberOfRecords()
    {
        var project = this.CreateProject();
        var first = this.Save(project, "/Content/Media/Car.glb", "/Content/Cars/Red.glb");
        var second = this.Save(project, "/Content/Media/Car.glb", "/Content/Cars/Blue.glb");
        var source = FileSystemPath.From(Path.Combine(ProjectRoot, "Content", "Media", "Car.glb"));

        var records = new ModelImportRecords(project);

        _ = records.RecordsNaming(source).Select(static record => record.RecordPath).Should().BeEquivalentTo([first, second]);
        _ = records.RecordsNaming(FileSystemPath.From(Path.Combine(ProjectRoot, "content", "media", "car.glb"))).Should().BeEmpty("a path that differs only in case names another file");
        _ = records.RecordsNaming(FileSystemPath.From(Path.Combine(ProjectRoot, "Content", "Other.glb"))).Should().BeEmpty();
    }

    [TestMethod]
    public void AModelIsReadAtItsIdentityWithoutListing()
    {
        var project = this.CreateProject();
        _ = this.Save(project, "/Content/Car.glb", "/Content/Car.glb");
        var damaged = ModelRecords.PathOf(project, "/Content/Broken.glb");
        this.fileSystem.File.WriteAllText(damaged, "{");

        _ = ModelImportRecords.TryRead(project, AssetPath.Parse("/Content/Car.glb"))!.Identity.Should().Be(AssetPath.Parse("/Content/Car.glb"));
        _ = ModelImportRecords.TryRead(project, AssetPath.Parse("/Content/Broken.glb")).Should().BeNull();
        _ = ModelImportRecords.TryRead(project, AssetPath.Parse("/Content/Gone.glb")).Should().BeNull();
        _ = ModelImportRecords.TryRead(project, AssetPath.Parse("/Content/Red.omat")).Should().BeNull("only a model has a record");
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
        _ = this.Save(project, "/Content/Car.glb", "/Content/Car.glb");
        var damaged = ModelRecords.PathOf(project, "/Content/Broken.glb");
        this.fileSystem.File.WriteAllText(damaged, "{");

        var records = new ModelImportRecords(project);

        _ = records.All.Should().ContainSingle();
        _ = records.Problems.Should().ContainSingle().Which.UnboundSource.Should().BeNull();
    }

    [TestMethod]
    public void TheIndexListsTheRecordsOfEverySourceMount()
    {
        var project = this.CreateProject();
        var car = this.Save(project, "/Content/Car.glb", "/Content/Car.glb");
        var ship = this.Save(project, "/Content/Models/Ships/Ship.glb", "/Content/Models/Ships/Ship.glb");
        var other = this.Save(project, "/Props/Deep/Er/Crate.glb", "/Props/Deep/Er/Crate.glb");

        var records = new ModelImportRecords(project);

        _ = records.Problems.Should().BeEmpty();
        _ = records.All.Select(static record => record.RecordPath).Should().BeEquivalentTo([car, ship, other]);
        _ = ship.Should().Be(Path.Combine(ProjectRoot, "Content", "Models", "Ships", "Ship.glb.import.json"));
    }

    [TestMethod]
    public void AFileThatNamesNoModelIsAProblemWithoutHidingTheOthers()
    {
        var project = this.CreateProject();
        _ = this.Save(project, "/Content/Car.glb", "/Content/Car.glb");
        var named = Path.Combine(ProjectRoot, "Content", "Models", "Truck.import.json");
        _ = this.fileSystem.Directory.CreateDirectory(Path.GetDirectoryName(named)!);
        this.fileSystem.File.WriteAllBytes(named, ModelRecords.Create("/Content/Truck.glb", "/Content/Truck.glb").ToBytes());

        var records = new ModelImportRecords(project);

        _ = records.All.Should().ContainSingle().Which.Identity.Should().Be(AssetPath.Parse("/Content/Car.glb"));
        _ = records.Problems.Should().ContainSingle().Which.RecordPath.Should().Be(named);
    }

    [TestMethod]
    public void ARecordInAnOutputFolderIsNotListed()
    {
        var project = this.CreateProject();
        var hidden = Path.Combine(ProjectRoot, "Content", ".cache", "Car.glb.import.json");
        _ = this.fileSystem.Directory.CreateDirectory(Path.GetDirectoryName(hidden)!);
        this.fileSystem.File.WriteAllText(hidden, "{");

        var records = new ModelImportRecords(project);

        _ = records.All.Should().BeEmpty();
        _ = records.Problems.Should().BeEmpty();
    }

    [TestMethod]
    public void ARecordInAMountOutsideTheProjectResolvesItsSourceThroughTheMountsFile()
    {
        _ = this.fileSystem.Directory.CreateDirectory(@"D:\Studio\SharedContent");
        _ = this.fileSystem.Directory.CreateDirectory(ProjectRoot);
        this.fileSystem.File.WriteAllText(Path.Combine(ProjectRoot, ProjectMounts.FileName), $$$"""{"schema_version":{{{ProjectMounts.SchemaVersion}}},"mounts":{"Studio":"D:/Studio/SharedContent"}}""");
        var project = TestProject.Create(this.fileSystem, ProjectRoot, [new ProjectMount("Content", Path: "Content"), new ProjectMount("Studio")]).Context;
        var path = this.Save(project, "/Studio/Vehicles/Car.glb", "/Studio/Vehicles/Car.glb");

        var records = new ModelImportRecords(project);

        _ = path.Should().Be(@"D:\Studio\SharedContent\Vehicles\Car.glb.import.json");
        _ = records.All.Should().ContainSingle().Which.SourcePath.Should().Be(@"D:\Studio\SharedContent\Vehicles\Car.glb");
    }

    [TestMethod]
    public void ARecordWhoseSourceMountIsNotBound_IsAProblemThatNamesItsSource()
    {
        var project = TestProject.Create(this.fileSystem, ProjectRoot, [new ProjectMount("Content", Path: "Content"), new ProjectMount("Studio")]).Context;
        _ = project.Mounts.Single(static mount => mount.Name == "Studio").State.Should().Be(MountState.Unbound);
        var path = this.Save(project, "/Studio/Vehicles/Car.glb", "/Content/Vehicles/Car.glb");

        var records = new ModelImportRecords(project);

        _ = records.All.Should().BeEmpty();
        var problem = records.Problems.Should().ContainSingle().Subject;
        _ = problem.RecordPath.Should().Be(path);
        _ = problem.UnboundSource.Should().Be("/Studio/Vehicles/Car.glb");
    }

    [TestMethod]
    public void ARecordThatNamesItsSourceOutsideItsMount_IsAProblem()
    {
        var project = this.CreateProject();
        var path = this.Save(project, "/Content/Car.glb", "/Content/Car.glb");
        var text = this.fileSystem.File.ReadAllText(path).Replace("\"source\": \"Car.glb\"", "\"source\": \"../../Content/Car.glb\"", StringComparison.Ordinal);
        this.fileSystem.File.WriteAllText(path, text);

        var records = new ModelImportRecords(project);

        _ = records.All.Should().BeEmpty();
        var problem = records.Problems.Should().ContainSingle().Subject;
        _ = problem.UnboundSource.Should().BeNull("the record is not readable: its source leaves its mount");
        _ = problem.RefusedSource.Should().Be("../../Content/Car.glb");
        _ = problem.RefusalCode.Should().Be("vpath.reference_outside_mount");
    }

    private ProjectContext CreateProject()
        => TestProject.Create(this.fileSystem, ProjectRoot, ["Content", "Props"]).Context;

    private string Save(ProjectContext project, string source, string identity)
        => ModelRecords.Write(project, ModelRecords.Create(identity, source));
}
