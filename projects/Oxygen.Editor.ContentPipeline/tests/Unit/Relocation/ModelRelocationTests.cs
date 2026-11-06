// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.TimeMachine;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Models;
using Oxygen.Editor.Models.Testing;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Relocation;

/// <summary>
/// A model is its record and its companions. Operations on it move, copy and delete those; a source anywhere else is only
/// referenced, and each operation leaves it where it is.
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class ModelRelocationTests
{
    private const string Lamp = "Content/Props/Lamp/Lamp.gltf";
    private const string Lantern = "Content/Props/Lamp/Lantern.gltf";

    public TestContext TestContext { get; set; } = null!;

    /// <summary>E1: a rename takes the record, the source and the preview; what the source reads stays, and so does its identity.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingAModelRenamesItsRecordSourceAndPreview()
    {
        using var workspace = new RelocationWorkspace();
        var model = workspace.WriteModel("Content/Props/Lamp", "Lamp.gltf");
        _ = workspace.Write(Lamp + AssetFileTypes.ModelPreview, "{}");

        _ = await this.MoveAsync(workspace, "/" + Lamp, "/" + Lantern).ConfigureAwait(false);

        _ = workspace.Exists(Lantern).Should().BeTrue();
        _ = workspace.Exists(Lantern + ".import.json").Should().BeTrue();
        _ = workspace.Exists(Lantern + AssetFileTypes.ModelPreview).Should().BeTrue();
        _ = workspace.Exists("Content/Props/Lamp/mesh.bin").Should().BeTrue();
        _ = workspace.Exists(Lamp).Should().BeFalse();
        var record = workspace.ReadRecord(model);
        _ = record.Identity.Should().Be(AssetPath.Parse("/" + Lantern));
        _ = record.Source.Should().Be("Lantern.gltf");
        _ = record.SourceIdentity.Should().Be(model.SourceIdentity);
    }

    /// <summary>A rename respells the references to the model and its products, and Undo restores everything.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingAModelRespellsItsProductsReferrersAndUndoRestores()
    {
        using var workspace = new RelocationWorkspace();
        _ = workspace.WriteModel("Content/Props/Lamp", "Lamp.gltf");
        const string material = "Content/Materials/Glass.omat.json";
        _ = workspace.Write(material, AuthoredFiles.Material("/Content/Materials/Glass.omat").Texture("base_color", "/Content/Props/Lamp/Lamp.gltf/Glass.otex").Text);
        var before = workspace.Snapshot();
        var history = new HistoryKeeper(this);

        _ = await workspace.Service.RelocateAsync(Move("/" + Lamp, "/" + Lantern), "Rename", history, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.Read(material).Should().Contain("../Props/Lamp/Lantern.gltf/Glass.otex");
        await history.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Snapshot().Should().BeEquivalentTo(before);
    }

    /// <summary>E2: a model whose source is elsewhere renames its record alone, and spells the source from where it now is.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingAModelOfASourceElsewhereRenamesTheRecordAlone()
    {
        using var workspace = new RelocationWorkspace();
        _ = workspace.Write("Content/Media/Truck.gltf", "{}");
        var model = WriteRecord(workspace, "/Content/Cars/Truck.gltf", "/Content/Media/Truck.gltf");

        _ = await this.MoveAsync(workspace, "/Content/Cars/Truck.gltf", "/Content/Cars/Van.gltf").ConfigureAwait(false);

        _ = workspace.Exists("Content/Media/Truck.gltf").Should().BeTrue("a source elsewhere is only referenced");
        _ = workspace.Exists("Content/Cars/Van.gltf.import.json").Should().BeTrue();
        _ = workspace.Exists("Content/Cars/Truck.gltf.import.json").Should().BeFalse();
        var record = workspace.ReadRecord(model);
        _ = record.Identity.Should().Be(AssetPath.Parse("/Content/Cars/Van.gltf"));
        _ = record.Source.Should().Be("../Media/Truck.gltf");
    }

    /// <summary>A model moved to another folder takes its companions, at their places below it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingAModelToAnotherFolderTakesItsCompanions()
    {
        using var workspace = new RelocationWorkspace();
        var model = workspace.WriteModel("Content/Props/Lamp", "Lamp.gltf");
        _ = workspace.Write("Content/Props/Lamp/notes.txt", "stays");

        _ = await this.MoveAsync(workspace, "/" + Lamp, "/Content/Props/Lamp.gltf").ConfigureAwait(false);

        _ = workspace.Exists("Content/Props/Lamp.gltf").Should().BeTrue();
        _ = workspace.Exists("Content/Props/Lamp.gltf.import.json").Should().BeTrue();
        _ = workspace.Exists("Content/Props/mesh.bin").Should().BeTrue();
        _ = workspace.Exists("Content/Props/Lamp/notes.txt").Should().BeTrue("a file the model does not read is not a companion");
        _ = workspace.ReadRecord(model).Source.Should().Be("Lamp.gltf");
    }

    /// <summary>A file the source reads moves its model, since the source names it by its place.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingACompanionMovesItsModel()
    {
        using var workspace = new RelocationWorkspace();
        var model = workspace.WriteModel("Content/Props/Lamp", "Lamp.gltf");
        _ = workspace.Write(Lamp + AssetFileTypes.ModelPreview, "{}");

        _ = await this.MoveAsync(workspace, "/Content/Props/Lamp/mesh.bin", "/Content/Props/mesh.bin").ConfigureAwait(false);

        _ = workspace.Exists("Content/Props/Lamp.gltf").Should().BeTrue();
        _ = workspace.Exists("Content/Props/mesh.bin").Should().BeTrue();
        _ = workspace.Exists("Content/Props/Lamp.gltf" + AssetFileTypes.ModelPreview).Should().BeTrue();
        _ = workspace.ReadRecord(model).Source.Should().Be("Lamp.gltf");
    }

    /// <summary>A file the source reads by its name keeps it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingACompanionIsRefused()
    {
        using var workspace = new RelocationWorkspace();
        _ = workspace.WriteModel("Content/Props/Lamp", "Lamp.gltf");

        var rename = () => this.MoveAsync(workspace, "/Content/Props/Lamp/mesh.bin", "/Content/Props/Lamp/body.bin");

        _ = await rename.Should().ThrowAsync<AssetRelocationException>().WithMessage("*reads 'mesh.bin' by its name*").ConfigureAwait(false);
    }

    /// <summary>A file two models read stays with both: moving one would separate the other from it.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task MovingAModelAwayFromAFileAnotherReadsIsRefused()
    {
        using var workspace = new RelocationWorkspace();
        _ = workspace.WriteModel("Content/Props/Lamp", "Lamp.gltf");
        _ = workspace.WriteModel("Content/Props/Lamp", "Shade.gltf");

        var move = () => this.MoveAsync(workspace, "/" + Lamp, "/Content/Props/Lamp.gltf");

        _ = await move.Should().ThrowAsync<AssetRelocationException>().WithMessage("*separate 'Shade.gltf'*").ConfigureAwait(false);
    }

    /// <summary>E3: a copy is a new record with a new source identity and its own companions; referrers are not rewritten.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CopyingAModelCopiesItsCompanionsWithANewIdentity()
    {
        using var workspace = new RelocationWorkspace();
        var model = workspace.WriteModel("Content/Props/Lamp", "Lamp.gltf");
        _ = workspace.Write("Content/Copies/keep.txt", "folder");
        const string material = "Content/Materials/Glass.omat.json";
        var text = AuthoredFiles.Material("/Content/Materials/Glass.omat").Texture("base_color", "/Content/Props/Lamp/Lamp.gltf/Glass.otex").Text;
        _ = workspace.Write(material, text);

        var copies = await workspace.Service.CopyAsync([AssetPath.Parse("/" + Lamp)], AssetPath.Parse("/Content/Copies"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = copies.Should().Equal(AssetPath.Parse("/Content/Copies/Lamp.gltf"));
        _ = workspace.Exists("Content/Copies/Lamp.gltf").Should().BeTrue();
        _ = workspace.Exists("Content/Copies/mesh.bin").Should().BeTrue();
        var copy = ModelImportSettings.Parse(System.Text.Encoding.UTF8.GetBytes(workspace.Read("Content/Copies/Lamp.gltf.import.json")), AssetPath.Parse("/Content/Copies/Lamp.gltf"));
        _ = copy.SourceIdentity.Should().NotBe(model.SourceIdentity);
        _ = copy.Source.Should().Be("Lamp.gltf");
        _ = workspace.Exists(Lamp).Should().BeTrue();
        _ = workspace.Read(material).Should().Be(text);
    }

    /// <summary>A duplicate beside the original has its own name and reads the files already there.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task DuplicatingAModelBesideItselfSharesWhatItReads()
    {
        using var workspace = new RelocationWorkspace();
        _ = workspace.WriteModel("Content/Props/Lamp", "Lamp.gltf");

        var copies = await workspace.Service.CopyAsync([AssetPath.Parse("/" + Lamp)], AssetPath.Parse("/Content/Props/Lamp"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = copies.Should().Equal(AssetPath.Parse("/Content/Props/Lamp/Lamp (2).gltf"));
        _ = workspace.Exists("Content/Props/Lamp/Lamp (2).gltf").Should().BeTrue();
        _ = workspace.Exists("Content/Props/Lamp/Lamp (2).gltf.import.json").Should().BeTrue();
        _ = workspace.Exists("Content/Props/Lamp/mesh (2).bin").Should().BeFalse();
    }

    /// <summary>E4: a duplicate of a model whose source is elsewhere is a new record naming the same source.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task DuplicatingAModelOfASourceElsewhereCopiesOnlyTheRecord()
    {
        using var workspace = new RelocationWorkspace();
        _ = workspace.Write("Content/Media/Truck.gltf", "{}");
        var model = WriteRecord(workspace, "/Content/Cars/Truck.gltf", "/Content/Media/Truck.gltf");

        _ = await workspace.Service.CopyAsync([AssetPath.Parse("/Content/Cars/Truck.gltf")], AssetPath.Parse("/Content/Cars"), this.TestContext.CancellationToken).ConfigureAwait(false);

        var copy = ModelImportSettings.Parse(System.Text.Encoding.UTF8.GetBytes(workspace.Read("Content/Cars/Truck (2).gltf.import.json")), AssetPath.Parse("/Content/Cars/Truck (2).gltf"));
        _ = copy.Source.Should().Be("../Media/Truck.gltf");
        _ = copy.SourceIdentity.Should().NotBe(model.SourceIdentity);
        _ = workspace.Exists("Content/Cars/Truck (2).gltf").Should().BeFalse("nothing but the record is copied");
    }

    /// <summary>A folder copied with a model gives the copy its own record.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CopyingAFolderGivesItsModelsNewIdentities()
    {
        using var workspace = new RelocationWorkspace();
        var model = workspace.WriteModel("Content/Props/Lamp", "Lamp.gltf");
        _ = workspace.Write("Content/Copies/keep.txt", "folder");

        _ = await workspace.Service.CopyAsync([AssetPath.Parse("/Content/Props/Lamp")], AssetPath.Parse("/Content/Copies"), this.TestContext.CancellationToken).ConfigureAwait(false);

        var copy = ModelImportSettings.Parse(System.Text.Encoding.UTF8.GetBytes(workspace.Read("Content/Copies/Lamp/Lamp.gltf.import.json")), AssetPath.Parse("/Content/Copies/Lamp/Lamp.gltf"));
        _ = copy.SourceIdentity.Should().NotBe(model.SourceIdentity);
        _ = copy.Source.Should().Be("Lamp.gltf");
    }

    /// <summary>E5: a delete recycles the record and companions, and leaves the rest of the folder.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task DeletingAModelRecyclesItsRecordAndCompanions()
    {
        using var workspace = new RelocationWorkspace();
        _ = workspace.WriteModel("Content/Props/Lamp", "Lamp.gltf");

        var recycled = await this.DeleteAsync(workspace, "/" + Lamp).ConfigureAwait(false);

        _ = recycled.Should().BeEquivalentTo(
            workspace.PathOf(Lamp),
            workspace.PathOf(Lamp + ".import.json"),
            workspace.PathOf("Content/Props/Lamp/mesh.bin"));
    }

    /// <summary>A companion another asset names stays.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task DeletingAModelKeepsWhatAnotherModelReads()
    {
        using var workspace = new RelocationWorkspace();
        _ = workspace.WriteModel("Content/Props/Lamp", "Lamp.gltf");
        _ = workspace.WriteModel("Content/Props/Lamp", "Shade.gltf");

        var recycled = await this.DeleteAsync(workspace, "/" + Lamp).ConfigureAwait(false);

        _ = recycled.Should().Contain(workspace.PathOf(Lamp)).And.NotContain(workspace.PathOf("Content/Props/Lamp/mesh.bin"));
    }

    /// <summary>E5: a delete never takes a source the model only references.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task DeletingAModelOfASourceElsewhereSparesTheSource()
    {
        using var workspace = new RelocationWorkspace();
        _ = workspace.Write("Content/Media/Truck.gltf", "{}");
        _ = WriteRecord(workspace, "/Content/Cars/Truck.gltf", "/Content/Media/Truck.gltf");

        var recycled = await this.DeleteAsync(workspace, "/Content/Cars/Truck.gltf").ConfigureAwait(false);

        _ = recycled.Should().Equal(workspace.PathOf("Content/Cars/Truck.gltf.import.json"));
    }

    /// <summary>A folder beside a model, not holding what it reads, is deleted like any folder.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task DeletingAFolderBesideAModelIsAllowed()
    {
        using var workspace = new RelocationWorkspace();
        _ = workspace.WriteModel("Content/Props", "Lamp.gltf");
        _ = workspace.Write("Content/Props/Other/notes.txt", "x");

        var recycled = await this.DeleteAsync(workspace, "/Content/Props/Other").ConfigureAwait(false);

        _ = recycled.Should().Equal(workspace.PathOf("Content/Props/Other"));
    }

    private static ModelImportSettings WriteRecord(RelocationWorkspace workspace, string identity, string source)
    {
        var record = ModelRecords.Create(identity, source, ModelImportSettings.DefaultSettings("Truck"));
        _ = ModelRecords.Write(workspace.Project, record);
        return record;
    }

    private static AssetRelocationRequest Move(string source, string target)
        => new() { Moves = [new(AssetPath.Parse(source), AssetPath.Parse(target))] };

    private Task<AssetRelocationPlan> MoveAsync(RelocationWorkspace workspace, string source, string target)
        => workspace.Service.RelocateAsync(Move(source, target), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken);

    private async Task<List<string>> DeleteAsync(RelocationWorkspace workspace, string source)
    {
        var recycled = new List<string>();
        await workspace.Service.DeleteAsync(
            [AssetPath.Parse(source)],
            paths =>
            {
                recycled.AddRange(paths);
                return Task.CompletedTask;
            },
            this.TestContext.CancellationToken).ConfigureAwait(false);
        return recycled;
    }
}
