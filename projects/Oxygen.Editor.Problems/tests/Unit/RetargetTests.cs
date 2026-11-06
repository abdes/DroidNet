// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using AwesomeAssertions;
using DroidNet.TimeMachine;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Problems.Unit.Tests.TestSupport;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Problems.Unit.Tests;

/// <summary>Retarget repairs an unresolved reference by picking the asset it should name, as one Undo step.</summary>
[TestClass]
internal sealed class RetargetTests : IDisposable
{
    private const string Written = "/Props/Furniture/Chair.fbx/Chair.ogeo";
    private static readonly AssetPath Picked = AssetPath.Parse("/Props/Seats/Chair.fbx/Chair.ogeo");

    private readonly TestSession session = TestProblems.CreateSession();
    private readonly Mock<IAssetRelocationService> relocation = new();
    private readonly Mock<IAssetFeed> assets = new();
    private readonly Mock<IDocumentArea> documents = new();
    private readonly Mock<IHistoryService> histories = new();
    private readonly FakeRuntimeProblems runtime = new();
    private readonly HistoryKeeper history;

    public RetargetTests()
    {
        this.history = new HistoryKeeper(this);
        _ = this.histories.SetupGet(static service => service.GlobalHistory).Returns(this.history);
        _ = this.relocation.Setup(static service => service.CanRetarget(It.IsAny<UnresolvedReference>())).Returns(value: true);
        _ = this.relocation.Setup(static service => service.FindUnsavedDocuments()).Returns([]);
        _ = this.assets.SetupGet(static feed => feed.Items).Returns(Observable.Return<IReadOnlyList<AssetEntry>>([Entry("/Props/Seats/Chair.fbx/Chair.ogeo", AssetKind.Geometry), Entry("/Props/Seats/Chair.omat", AssetKind.Material)]));
        _ = this.documents
            .Setup(static area => area.SaveBeforeAsync(It.IsAny<SaveBeforeOperation>(), It.IsAny<Func<CancellationToken, Task<IReadOnlyList<UnsavedDocument>>>>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(value: true);
    }

    /// <inheritdoc/>
    public void Dispose() => this.session.Dispose();

    [TestMethod]
    public void OnlyAReferenceTheProjectCanRewriteOffersRetarget()
    {
        var reference = Reference("Garage", "$.nodes[0]");
        _ = this.relocation.Setup(service => service.CanRetarget(reference)).Returns(value: false);
        using var model = this.CreateModel(reference);

        model.Selected = model.Groups[0].Entries[0];

        _ = model.CanRetarget.Should().BeFalse();
    }

    [TestMethod]
    public void AReferenceOfNoKnownTypeOffersNoRetarget()
    {
        using var model = this.CreateModel(Reference("Garage", "$.nodes[0]") with { Written = "not an asset" });

        model.Selected = model.Groups[0].Entries[0];

        _ = model.CanRetarget.Should().BeFalse();
    }

    [TestMethod]
    public void ThePickerOffersTheFieldsTypeAndSearchesTheBrokenName()
    {
        using var model = this.CreateModel(Reference("Garage", "$.nodes[0]"));
        model.Selected = model.Groups[0].Entries[0];

        model.BeginRetarget();

        _ = model.CanRetarget.Should().BeTrue();
        _ = model.RetargetChoices.SearchText.Should().Be("Chair");
        _ = model.RetargetChoices.Groups.SelectMany(static group => group.Rows).Select(static row => row.Choice.Asset).Should().Equal(Picked);
        _ = model.OtherReferences.Should().BeEmpty();
    }

    [TestMethod]
    public async Task APickRewritesTheOtherReferencesWithTheSameTextInOneStep()
    {
        var first = Reference("Garage", "$.nodes[0]");
        var second = Reference("Garage", "$.nodes[1]");
        var third = Reference("Dock", "$.nodes[0]");
        using var model = this.CreateModel(first, second, third, Reference("Dock", "$.nodes[1]") with { Written = "/Props/Oak.omat", Resolved = "/Props/Oak.omat" });
        model.Selected = model.Groups.SelectMany(static group => group.Entries).First(entry => entry.Problem is UnresolvedReferenceProblem { Reference: var reference } && reference == first);
        model.BeginRetarget();

        _ = model.RetargetsOthers.Should().BeTrue("the option starts checked");
        _ = model.OtherReferencesText.Should().Be("Also the 2 other references that name this path");
        await model.RetargetAsync(Picked).ConfigureAwait(false);

        this.relocation.Verify(
            service => service.RetargetAsync(
                It.Is<IReadOnlyList<UnresolvedReference>>(references => references.Count == 3 && references[0] == first && references.Contains(second) && references.Contains(third)),
                Picked,
                "Retarget",
                this.history,
                It.IsAny<CancellationToken>()),
            Times.Once);
    }

    [TestMethod]
    public async Task UncheckingTheOptionRewritesOnlyTheSelectedReference()
    {
        var first = Reference("Garage", "$.nodes[0]");
        using var model = this.CreateModel(first, Reference("Dock", "$.nodes[0]"));
        model.Selected = model.Groups.SelectMany(static group => group.Entries).First(entry => entry.Problem is UnresolvedReferenceProblem { Reference: var reference } && reference == first);
        model.BeginRetarget();
        model.RetargetsOthers = false;

        await model.RetargetAsync(Picked).ConfigureAwait(false);

        this.relocation.Verify(
            service => service.RetargetAsync(
                It.Is<IReadOnlyList<UnresolvedReference>>(references => references.Count == 1 && references[0] == first),
                Picked,
                It.IsAny<string>(),
                It.IsAny<HistoryKeeper>(),
                It.IsAny<CancellationToken>()),
            Times.Once);
    }

    [TestMethod]
    public async Task DocumentsNotSavedChangeNothingAndSaySo()
    {
        _ = this.documents
            .Setup(static area => area.SaveBeforeAsync(SaveBeforeOperation.Retarget, It.IsAny<Func<CancellationToken, Task<IReadOnlyList<UnsavedDocument>>>>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(value: false);
        using var model = this.CreateModel(Reference("Garage", "$.nodes[0]"));
        model.Selected = model.Groups[0].Entries[0];

        await model.RetargetAsync(Picked).ConfigureAwait(false);

        _ = model.HasRetargetFailure.Should().BeTrue();
        this.relocation.Verify(
            static service => service.RetargetAsync(It.IsAny<IReadOnlyList<UnresolvedReference>>(), It.IsAny<AssetPath>(), It.IsAny<string>(), It.IsAny<HistoryKeeper>(), It.IsAny<CancellationToken>()),
            Times.Never);
    }

    [TestMethod]
    public async Task ARefusedRetargetSaysWhy()
    {
        _ = this.relocation
            .Setup(static service => service.RetargetAsync(It.IsAny<IReadOnlyList<UnresolvedReference>>(), It.IsAny<AssetPath>(), It.IsAny<string>(), It.IsAny<HistoryKeeper>(), It.IsAny<CancellationToken>()))
            .ThrowsAsync(new AssetRelocationException("Garage.oscene.json changed."));
        using var model = this.CreateModel(Reference("Garage", "$.nodes[0]"));
        model.Selected = model.Groups[0].Entries[0];

        await model.RetargetAsync(Picked).ConfigureAwait(false);

        _ = model.RetargetFailure.Should().Be("Garage.oscene.json changed.");
    }

    private static UnresolvedReference Reference(string scene, string property)
        => new(FileSystemPath.From($@"C:\Project\Content\Levels\{scene}.oscene.json"), property, Written, Written, "reference.unresolved");

    private static AssetEntry Entry(string identity, AssetKind kind)
    {
        var path = AssetPath.Parse(identity);
        return new(path, path.Name, kind, AssetState.Source, DerivedState: null, AssetRuntimeAvailability.NotApplicable, identity, SourcePath: FileSystemPath.From(@"C:\Project" + identity.Replace('/', '\\')), DescriptorPath: null, CookedPath: null, AssetGuid: null, DiagnosticCodes: [], IsSelectable: true);
    }

    private ProblemsViewModel CreateModel(params UnresolvedReference[] references)
    {
        var source = new Mock<IAssetProblemSource>();
        _ = source.SetupGet(static problems => problems.Problems).Returns([.. references.Select(static reference => new UnresolvedReferenceProblem(reference))]);
        return TestProblems.CreateModel(
            this.session,
            [source.Object],
            Mock.Of<ICodeEditorService>(),
            this.runtime,
            retarget: new(this.relocation.Object, this.assets.Object, this.histories.Object, this.documents.Object));
    }
}
