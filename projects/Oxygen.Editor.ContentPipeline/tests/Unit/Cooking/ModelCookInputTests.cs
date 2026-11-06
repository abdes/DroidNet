// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Models.Testing;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

/// <summary>
/// A model is its record's identity: the cook, a folder's cook and a document all reach its source through the record,
/// which may name a file anywhere; a model file without a record is not imported, and no file is cooked twice.
/// </summary>
[TestClass]
internal sealed class ModelCookInputTests
{
    private static readonly AssetPath Besides = AssetPath.Parse("/Content/Cars/Car.glb");
    private static readonly AssetPath Elsewhere = AssetPath.Parse("/Content/Models/Truck.glb");
    private static readonly AssetPath NotImported = AssetPath.Parse("/Content/Raw/Bus.glb");

    private readonly TestProject testProject = TestProject.Create(new MockFileSystem());

    public ModelCookInputTests()
    {
        _ = this.testProject.WriteFile(Besides.VirtualPath, "glTF");
        _ = this.testProject.WriteFile("/Content/Media/Truck.fbx", "fbx");
        _ = this.testProject.WriteFile(NotImported.VirtualPath, "glTF");
        _ = ModelRecords.Write(this.Project, Besides.VirtualPath, Besides.VirtualPath);
        _ = ModelRecords.Write(this.Project, Elsewhere.VirtualPath, "/Content/Media/Truck.fbx");
    }

    private ProjectContext Project => this.testProject.Context;

    [TestMethod]
    public void AModelCooksFromTheFileItsRecordNames()
    {
        var input = CookInputResolver.Resolve(this.Project, Elsewhere, ContentCookInputRole.Primary);

        _ = input.AssetUri.Should().Be(Elsewhere);
        _ = input.Kind.Should().Be(ContentCookAssetKind.ForeignSource);
        _ = input.SourceCookPath.Should().Be("/Content/Media/Truck.fbx");
    }

    [TestMethod]
    public void ARecordsPathNamesItsModel()
    {
        var input = CookInputResolver.Resolve(this.Project, AssetPath.Parse("/Content/Models/Truck.glb.import.json"), ContentCookInputRole.Primary);

        _ = input.AssetUri.Should().Be(Elsewhere);
        _ = input.SourceCookPath.Should().Be("/Content/Media/Truck.fbx");
    }

    [TestMethod]
    public void AProductResolvesToItsImportedModelOnly()
    {
        _ = CookInputResolver.ResolveContainer(this.Project, Elsewhere.Combine("Body.ogeo"), ContentCookInputRole.Primary)!.AssetUri.Should().Be(Elsewhere);
        _ = CookInputResolver.ResolveContainer(this.Project, NotImported.Combine("Body.ogeo"), ContentCookInputRole.Primary).Should().BeNull("a model produces nothing until it has a record");
    }

    [TestMethod]
    public void AModelCooksOnceFromAFolderScan()
    {
        bool Cookable(AssetPath path) => ContentPipelineService.IsCookableDescriptorFile(this.Project, this.Project.Find(path)!.File.FullPath);

        _ = Cookable(Besides).Should().BeTrue("the model's file stands for it");
        _ = Cookable(Besides.WithName("Car.glb.import.json")).Should().BeFalse("its file already stands for it");
        _ = Cookable(Elsewhere.WithName("Truck.glb.import.json")).Should().BeTrue("no file is at its identity");
        _ = Cookable(NotImported).Should().BeFalse("it has no record");
        _ = Cookable(AssetPath.Parse("/Content/Media/Truck.fbx")).Should().BeFalse("a record elsewhere makes another model of it");
    }

    [TestMethod]
    public void AModelsDocumentOpensOnItsSourceAtItsIdentity()
    {
        var session = new Mock<IProjectSession>();
        _ = session.SetupGet(static value => value.Context).Returns(this.Project);
        var files = new ProjectDocumentFiles(session.Object);

        var document = files.Find(Elsewhere)!;
        _ = document.Asset.Should().Be(Elsewhere);
        _ = document.Path.Should().Be(this.Project.Find(AssetPath.Parse("/Content/Media/Truck.fbx"))!.File);

        var besides = files.Find(Besides)!;
        _ = besides.Asset.Should().Be(Besides);
        _ = besides.Path.Should().Be(this.Project.Find(Besides)!.File);

        _ = files.Find(NotImported)!.Asset.Should().Be(NotImported, "a model not yet imported still opens on its own file");
    }
}
