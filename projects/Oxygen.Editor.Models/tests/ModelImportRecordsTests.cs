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
        this.fileSystem.File.WriteAllText(ModelImportSettings.PathFor(ProjectRoot, Guid.CreateVersion7()), "{");

        var records = new ModelImportRecords(this.fileSystem, ProjectRoot);

        _ = records.All.Should().ContainSingle();
        _ = records.Problems.Should().ContainSingle();
    }

    private string Save(string source, string virtualPath)
    {
        var path = ModelImportSettings.PathFor(ProjectRoot, Guid.CreateVersion7());
        using var settings = JsonDocument.Parse("{}");
        _ = this.fileSystem.Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        this.fileSystem.File.WriteAllBytes(path, ModelImportSettings.Create(path, source, virtualPath, settings.RootElement).ToBytes());
        return path;
    }
}
