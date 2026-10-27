// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Documents;
using DroidNet.Storage;
using DroidNet.TimeMachine;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Models.Documents;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Models.Tests.Documents;

/// <summary>
/// The model document (model editor 4): <see cref="ModelDocumentKind"/> opens a model source file, titled with its
/// extension, and its content names the model, which the editor never writes.
/// </summary>
[TestClass]
public sealed class ModelDocumentKindTests : IDisposable
{
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Vehicles/Car.glb");
    private static readonly AssetPath MovedCar = AssetPath.Parse("/Content/Cars/Car.glb");

    // A project without records: these tests read no import record.
    private readonly TestSession session = new(TestProject.Create(new MockFileSystem()).Context);

    public TestContext TestContext { get; set; }

    [TestMethod]
    [DataRow("Car.glb")]
    [DataRow("Car.gltf")]
    [DataRow("Car.fbx")]
    public void TitleOf_ModelSource_KeepsTheExtension(string name)
        => _ = DocumentKinds.TitleOf(this.Kind(), PathOf(name)).Should().Be(name);

    [TestMethod]
    public void SuffixOf_SceneFile_IsNotAModel()
        => _ = DocumentKinds.SuffixOf(this.Kind(), PathOf("Harbor.oscene.json")).Should().BeNull();

    [TestMethod]
    public async Task Decode_ModelSource_NamesTheModelWithoutReadingIt()
    {
        var result = await this.Kind().DecodeAsync(
            new DocumentFile(PathOf("Car.glb"), Car),
            ReadOnlyMemory<byte>.Empty,
            new DocumentDecodeContext(DocumentId.New(), new HistoryKeeper(new object())),
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Content.Should().BeOfType<ModelContent>().Which.Source.Should().Be(Car);
    }

    [TestMethod]
    public void Capture_WithoutARecord_IsRefused()
    {
        var model = new ModelContent(Car);

        _ = model.Capture().Refusal.Should().NotBeNull();
        _ = model.CaptureCopy(new DocumentFile(PathOf("Copy.glb"), MovedCar)).Refusal.Should().NotBeNull();
        _ = model.HasEditsInProgress.Should().BeFalse();
    }

    [TestMethod]
    public void ApplyRelocation_FollowsTheSourceAndRaisesRelocated()
    {
        var model = new ModelContent(Car);
        var relocation = new DocumentRelocation(new DocumentFile(PathOf("Car.glb"), MovedCar), FileVersion.Missing, asset => asset == Car ? MovedCar : null);
        DocumentRelocation? raised = null;
        model.Relocated += (_, args) => raised = args.Relocation;

        model.ApplyRelocation(relocation);

        _ = model.Source.Should().Be(MovedCar);
        _ = raised.Should().BeSameAs(relocation);
    }

    public void Dispose() => this.session.Dispose();

    private ModelDocumentKind Kind() => new(this.session);

    private static FileSystemPath PathOf(string name) => FileSystemPath.From(Path.Combine(Path.GetTempPath(), name));
}
