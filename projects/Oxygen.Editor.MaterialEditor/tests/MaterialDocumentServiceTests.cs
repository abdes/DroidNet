// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Assets.Import.Materials;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.ContentPipeline;

namespace Oxygen.Editor.MaterialEditor.Tests;

[TestClass]
public sealed class MaterialDocumentServiceTests
{
    [TestMethod]
    public async Task CreateEditSaveOpen_ShouldRoundTripScalarFields()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Test.omat.json");
        var service = CreateService(workspace);

        var created = await service.CreateAsync(materialUri).ConfigureAwait(false);
        var edit = await service.EditScalarAsync(
            created.DocumentId,
            new MaterialFieldEdit(MaterialFieldKeys.MetallicFactor, 0.75f)).ConfigureAwait(false);

        _ = edit.Succeeded.Should().BeTrue();
        var save = await service.SaveAsync(created.DocumentId).ConfigureAwait(false);
        _ = save.Succeeded.Should().BeTrue();

        await service.CloseAsync(created.DocumentId, discard: false).ConfigureAwait(false);
        var reopened = await service.OpenAsync(materialUri).ConfigureAwait(false);

        _ = reopened.MaterialGuid.Should().Be(created.MaterialGuid);
        _ = reopened.Source.Schema.Should().Be("oxygen.material.v1");
        _ = reopened.Source.PbrMetallicRoughness.MetallicFactor.Should().Be(0.75f);
        _ = reopened.Source.PbrMetallicRoughness.RoughnessFactor.Should().Be(0.5f);
    }

    [TestMethod]
    public async Task CreateAsync_ShouldUseAssetFileStemAsMaterialName()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Gold.omat.json");
        var service = CreateService(workspace);

        var created = await service.CreateAsync(materialUri).ConfigureAwait(false);

        _ = created.DisplayName.Should().Be("Gold");
        _ = created.MaterialGuid.Should().NotBe(Guid.Empty);
        _ = created.Source.Name.Should().Be("Gold");
        _ = ReadMaterial(workspace, "Content/Materials/Gold.omat.json").Name.Should().Be("Gold");
    }

    [TestMethod]
    public async Task SaveAsync_WhenDescriptorNameDiffersFromAssetFileStem_ShouldPersistFileStemName()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Gold.omat.json");
        await WriteMaterialAsync(workspace, "Content/Materials/Gold.omat.json", "Old Descriptor Name").ConfigureAwait(false);
        var service = CreateService(workspace);

        var opened = await service.OpenAsync(materialUri).ConfigureAwait(false);
        _ = opened.DisplayName.Should().Be("Gold");
        _ = opened.Source.Name.Should().Be("Gold");

        var save = await service.SaveAsync(opened.DocumentId).ConfigureAwait(false);

        _ = save.Succeeded.Should().BeTrue();
        _ = ReadMaterial(workspace, "Content/Materials/Gold.omat.json").Name.Should().Be("Gold");
    }

    [TestMethod]
    public async Task EditScalarAsync_ShouldClampOutOfRangeScalarFields()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Test.omat.json");
        var service = CreateService(workspace);

        var created = await service.CreateAsync(materialUri).ConfigureAwait(false);
        var edit = await service.EditScalarAsync(
            created.DocumentId,
            new MaterialFieldEdit(MaterialFieldKeys.BaseColorR, 2.0f)).ConfigureAwait(false);
        _ = edit.Succeeded.Should().BeTrue();

        edit = await service.EditScalarAsync(
            created.DocumentId,
            new MaterialFieldEdit(MaterialFieldKeys.RoughnessFactor, -1.0f)).ConfigureAwait(false);
        _ = edit.Succeeded.Should().BeTrue();

        await service.SaveAsync(created.DocumentId).ConfigureAwait(false);
        await service.CloseAsync(created.DocumentId, discard: false).ConfigureAwait(false);

        var reopened = await service.OpenAsync(materialUri).ConfigureAwait(false);

        _ = reopened.Source.PbrMetallicRoughness.BaseColorR.Should().Be(1.0f);
        _ = reopened.Source.PbrMetallicRoughness.RoughnessFactor.Should().Be(0.0f);
    }

    [TestMethod]
    public async Task CookAsync_ShouldUseMaterialCookServiceAndUpdateState()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Test.omat.json");
        var cook = new RecordingCookService();
        var service = new MaterialDocumentService(new TestResolver(workspace.Root), cook);

        var created = await service.CreateAsync(materialUri).ConfigureAwait(false);
        var result = await service.CookAsync(created.DocumentId).ConfigureAwait(false);

        _ = result.State.Should().Be(MaterialCookState.Cooked);
        _ = cook.LastRequest.Should().NotBeNull();
        _ = cook.LastRequest!.SourceRelativePath.Should().Be("Content/Materials/Test.omat.json");
    }

    [TestMethod]
    public async Task CookAsync_WhenDocumentIsDirty_ShouldRejectWithoutCallingCookService()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Test.omat.json");
        var cook = new RecordingCookService();
        var publisher = new RecordingOperationPublisher();
        var service = new MaterialDocumentService(new TestResolver(workspace.Root), cook, publisher);

        var created = await service.CreateAsync(materialUri).ConfigureAwait(false);
        _ = await service.EditScalarAsync(
            created.DocumentId,
            new MaterialFieldEdit(MaterialFieldKeys.MetallicFactor, 0.25f)).ConfigureAwait(false);

        var result = await service.CookAsync(created.DocumentId).ConfigureAwait(false);

        _ = result.State.Should().Be(MaterialCookState.Rejected);
        _ = result.OperationId.Should().NotBeNull();
        _ = cook.LastRequest.Should().BeNull();
        _ = publisher.Published.Should().ContainSingle(r => r.Diagnostics.Single().Code == MaterialDiagnosticCodes.DescriptorDirty);
    }

    [TestMethod]
    public async Task EditScalarAsync_WhenNameIsEdited_ShouldRejectBecauseAssetFileStemIsCanonical()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Test.omat.json");
        var publisher = new RecordingOperationPublisher();
        var service = new MaterialDocumentService(new TestResolver(workspace.Root), new RecordingCookService(), publisher);

        var created = await service.CreateAsync(materialUri).ConfigureAwait(false);
        var result = await service.EditScalarAsync(
            created.DocumentId,
            new MaterialFieldEdit(MaterialFieldKeys.Name, "Renamed")).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.OperationId.Should().NotBeNull();
        _ = publisher.Published.Should().ContainSingle(r =>
            r.OperationKind == MaterialOperationKinds.EditScalar
            && r.Diagnostics.Single().Domain == FailureDomain.MaterialAuthoring
            && r.Diagnostics.Single().Code == MaterialDiagnosticCodes.FieldRejected);
    }

    private static MaterialSource ReadMaterial(TempWorkspace workspace, string relativePath)
    {
        var bytes = File.ReadAllBytes(Path.Combine(workspace.Root, relativePath));
        return MaterialSourceReader.Read(bytes);
    }

    private static async Task WriteMaterialAsync(TempWorkspace workspace, string relativePath, string name)
    {
        var source = new MaterialSource(
            schema: "oxygen.material.v1",
            type: "PBR",
            name: name,
            pbrMetallicRoughness: new MaterialPbrMetallicRoughness(
                baseColorR: 1.0f,
                baseColorG: 1.0f,
                baseColorB: 1.0f,
                baseColorA: 1.0f,
                metallicFactor: 0.0f,
                roughnessFactor: 0.5f,
                baseColorTexture: null,
                metallicRoughnessTexture: null),
            normalTexture: null,
            occlusionTexture: null,
            alphaMode: MaterialAlphaMode.Opaque,
            alphaCutoff: 0.5f,
            doubleSided: false);

        var path = Path.Combine(workspace.Root, relativePath);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var stream = File.Create(path);
        MaterialSourceWriter.Write(stream, source);
        await stream.FlushAsync().ConfigureAwait(false);
    }

    private static MaterialDocumentService CreateService(TempWorkspace workspace)
        => new(new TestResolver(workspace.Root), new RecordingCookService());

    private sealed class RecordingCookService : IMaterialCookService
    {
        public MaterialCookRequest? LastRequest { get; private set; }

        public Task<MaterialCookResult> CookMaterialAsync(MaterialCookRequest request, CancellationToken cancellationToken = default)
        {
            this.LastRequest = request;
            return Task.FromResult(
                new MaterialCookResult(
                    request.MaterialSourceUri,
                    new Uri("asset:///Content/Materials/Test.omat"),
                    MaterialCookState.Cooked,
                    OperationId: null));
        }

        public Task<MaterialCookState> GetMaterialCookStateAsync(
            Uri materialSourceUri,
            CancellationToken cancellationToken = default)
            => Task.FromResult(MaterialCookState.NotCooked);
    }

    private sealed class RecordingOperationPublisher : IOperationResultPublisher
    {
        public List<OperationResult> Published { get; } = [];

        public IDisposable Subscribe(IObserver<OperationResult> observer)
        {
            _ = observer;
            return new EmptyDisposable();
        }

        public void Publish(OperationResult result) => this.Published.Add(result);
    }

    private sealed class EmptyDisposable : IDisposable
    {
        public void Dispose()
        {
        }
    }

    private sealed class TestResolver(string projectRoot) : IMaterialSourcePathResolver
    {
        public MaterialSourceLocation Resolve(Uri materialUri)
        {
            var relative = materialUri.AbsolutePath.TrimStart('/');
            return new MaterialSourceLocation(
                materialUri,
                projectRoot,
                "Content",
                Path.Combine(projectRoot, relative),
                relative);
        }
    }

    private sealed class TempWorkspace : IDisposable
    {
        public TempWorkspace()
        {
            this.Root = Path.Combine(Path.GetTempPath(), "oxygen-material-editor-tests", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(this.Root);
        }

        public string Root { get; }

        public void Dispose()
        {
            if (Directory.Exists(this.Root))
            {
                Directory.Delete(this.Root, recursive: true);
            }
        }
    }
}
