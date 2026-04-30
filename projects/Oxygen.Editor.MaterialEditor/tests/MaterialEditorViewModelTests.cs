// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Assets.Import.Materials;
using Oxygen.Assets.Model;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Schemas;
using Windows.UI;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>
/// Tests schema-property editing from the material editor view model.
/// </summary>
[TestClass]
public sealed class MaterialEditorViewModelTests
{
    /// <summary>
    /// Verifies scalar edits use the schema-driven service entry point.
    /// </summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task ScalarPropertyChangesUseSchemaPropertyEdit()
    {
        var service = new RecordingDocumentService(CreateDocument());
        using var sut = new MaterialEditorViewModel(new MaterialDocumentMetadata(service.Document.MaterialUri), service);
        await WaitForLoadAsync(sut).ConfigureAwait(false);

        sut.MetallicFactor = 0.75f;
        await WaitForEditAsync(service, expectedCount: 1).ConfigureAwait(false);

        _ = service.ScalarEditCalls.Should().Be(0);
        _ = service.PropertyEdits.Should().ContainSingle();
        _ = service.PropertyEdits[0].GetTyped(MaterialDescriptors.Metalness, out var value).Should().BeTrue();
        _ = value.Should().Be(0.75f);
        _ = sut.IsDirty.Should().BeTrue();
        _ = sut.CookState.Should().Be(MaterialCookState.Stale);
    }

    /// <summary>
    /// Verifies the color picker commits all four base-color channels as one property edit.
    /// </summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task SetBaseColorBatchesChannelsIntoOneSchemaPropertyEdit()
    {
        var service = new RecordingDocumentService(CreateDocument());
        using var sut = new MaterialEditorViewModel(new MaterialDocumentMetadata(service.Document.MaterialUri), service);
        await WaitForLoadAsync(sut).ConfigureAwait(false);

        sut.SetBaseColor(Color.FromArgb(128, 64, 128, 255));
        await WaitForEditAsync(service, expectedCount: 1).ConfigureAwait(false);

        var edit = service.PropertyEdits.Should().ContainSingle().Which;
        _ = edit.Count.Should().Be(4);
        _ = edit.GetTyped(MaterialDescriptors.BaseColorR, out var r).Should().BeTrue();
        _ = edit.GetTyped(MaterialDescriptors.BaseColorG, out var g).Should().BeTrue();
        _ = edit.GetTyped(MaterialDescriptors.BaseColorB, out var b).Should().BeTrue();
        _ = edit.GetTyped(MaterialDescriptors.BaseColorA, out var a).Should().BeTrue();
        _ = r.Should().BeApproximately(64.0f / 255.0f, 0.0001f);
        _ = g.Should().BeApproximately(128.0f / 255.0f, 0.0001f);
        _ = b.Should().Be(1.0f);
        _ = a.Should().BeApproximately(128.0f / 255.0f, 0.0001f);
        _ = service.ScalarEditCalls.Should().Be(0);
    }

    private static async Task WaitForLoadAsync(MaterialEditorViewModel viewModel)
    {
        await WaitUntilAsync(() => string.Equals(viewModel.StatusText, "Cook: NotCooked", StringComparison.Ordinal)).ConfigureAwait(false);
    }

    private static async Task WaitForEditAsync(RecordingDocumentService service, int expectedCount)
    {
        await WaitUntilAsync(() => service.PropertyEditCount >= expectedCount).ConfigureAwait(false);
    }

    private static async Task WaitUntilAsync(Func<bool> condition)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        while (!condition())
        {
            timeout.Token.ThrowIfCancellationRequested();
            await Task.Delay(10, timeout.Token).ConfigureAwait(false);
        }
    }

    private static MaterialDocument CreateDocument()
    {
        var uri = new Uri("asset:///Content/Materials/Test.omat.json");
        var source = new MaterialSource(
            schema: "oxygen.material.v1",
            type: "PBR",
            name: "Test",
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

        return new MaterialDocument(
            DocumentId: Guid.NewGuid(),
            MaterialUri: uri,
            MaterialGuid: Guid.NewGuid(),
            SourcePath: @"C:\Project\Content\Materials\Test.omat.json",
            DisplayName: "Test",
            Source: source,
            Asset: new MaterialAsset { Uri = uri, Source = source },
            IsDirty: false,
            CookState: MaterialCookState.NotCooked);
    }

    private sealed class RecordingDocumentService(MaterialDocument document) : IMaterialDocumentService
    {
        private readonly Lock sync = new();
        private readonly List<PropertyEdit> propertyEdits = [];

        public MaterialDocument Document { get; } = document;

        public List<PropertyEdit> PropertyEdits
        {
            get
            {
                lock (this.sync)
                {
                    return this.propertyEdits.Select(static edit => edit.Clone()).ToList();
                }
            }
        }

        public int PropertyEditCount
        {
            get
            {
                lock (this.sync)
                {
                    return this.propertyEdits.Count;
                }
            }
        }

        public int ScalarEditCalls { get; private set; }

        public Task<MaterialDocument> CreateAsync(Uri targetUri, CancellationToken cancellationToken = default)
        {
            _ = targetUri;
            cancellationToken.ThrowIfCancellationRequested();
            return Task.FromResult(this.Document);
        }

        public Task<MaterialDocument> OpenAsync(Uri sourceUri, CancellationToken cancellationToken = default)
        {
            _ = sourceUri;
            cancellationToken.ThrowIfCancellationRequested();
            return Task.FromResult(this.Document);
        }

        public Task<MaterialEditResult> EditScalarAsync(
            Guid documentId,
            MaterialFieldEdit edit,
            CancellationToken cancellationToken = default)
        {
            _ = documentId;
            _ = edit;
            cancellationToken.ThrowIfCancellationRequested();
            this.ScalarEditCalls++;
            return Task.FromResult(new MaterialEditResult(Succeeded: false, OperationId: null));
        }

        public Task<MaterialEditResult> EditPropertiesAsync(
            Guid documentId,
            PropertyEdit edit,
            CancellationToken cancellationToken = default)
        {
            _ = documentId;
            cancellationToken.ThrowIfCancellationRequested();
            lock (this.sync)
            {
                this.propertyEdits.Add(edit.Clone());
            }

            return Task.FromResult(new MaterialEditResult(Succeeded: true, OperationId: null));
        }

        public Task<MaterialSaveResult> SaveAsync(Guid documentId, CancellationToken cancellationToken = default)
        {
            _ = documentId;
            cancellationToken.ThrowIfCancellationRequested();
            return Task.FromResult(new MaterialSaveResult(Succeeded: true, OperationId: null));
        }

        public Task<MaterialCookResult> CookAsync(Guid documentId, CancellationToken cancellationToken = default)
        {
            _ = documentId;
            cancellationToken.ThrowIfCancellationRequested();
            return Task.FromResult(new MaterialCookResult(
                this.Document.MaterialUri,
                CookedMaterialUri: null,
                MaterialCookState.NotCooked,
                OperationId: null));
        }

        public Task CloseAsync(Guid documentId, bool discard, CancellationToken cancellationToken = default)
        {
            _ = documentId;
            _ = discard;
            cancellationToken.ThrowIfCancellationRequested();
            return Task.CompletedTask;
        }
    }
}
