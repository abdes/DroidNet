// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Assets.Import.Materials;
using Oxygen.Managed.Assets.Model;
using Windows.UI;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>
/// Tests schema-property editing from the material editor view model.
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers these public test classes using the repository's default discovery configuration.")]
public sealed partial class MaterialEditorViewModelTests
{
    /// <summary>Gets or sets cancellation for asynchronous regression tests.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Verifies saving a snapshot keeps newer edits dirty and requires another save before closing.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task SaveAcknowledgesSnapshotWhileNewerUiEditsRemainDirty()
    {
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var service = new RecordingDocumentService(CreateDocument()) { PendingWrite = release.Task };
        var metadata = new MaterialDocumentMetadata(service.Document.MaterialUri);
        using var sut = new MaterialEditorViewModel(metadata, service, Oxygen.Testing.AssetStatusFixture.EmptyProvider, System.Reactive.Concurrency.ImmediateScheduler.Instance);
        await WaitForLoadAsync(sut).ConfigureAwait(false);
        sut.MetallicFactor = 0.25f;
        var save = sut.SaveAsync();
        await service.WriteStarted.Task.ConfigureAwait(false);
        sut.MetallicFactor = 0.75f;
        _ = service.PropertyEditCount.Should().Be(2);
        release.SetResult();
        await save.ConfigureAwait(false);
        _ = sut.MetallicFactor.Should().Be(0.75f);
        _ = sut.IsDirty.Should().BeTrue();
        _ = metadata.IsDirty.Should().BeTrue();
        _ = sut.StatusText.Should().Be("Saved; newer changes remain unsaved");
        await sut.PrepareForCloseAsync().ConfigureAwait(false);
        _ = (await sut.SaveForCloseAsync().ConfigureAwait(false)).Should().BeTrue();
        _ = metadata.IsDirty.Should().BeFalse();
    }

    /// <summary>Verifies resource disposal never makes a material close decision.</summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task DisposalDoesNotCloseOrDiscardTheMaterial()
    {
        var service = new RecordingDocumentService(CreateDocument());
        var sut = new MaterialEditorViewModel(new MaterialDocumentMetadata(service.Document.MaterialUri), service, Oxygen.Testing.AssetStatusFixture.EmptyProvider, System.Reactive.Concurrency.ImmediateScheduler.Instance);
        await WaitForLoadAsync(sut).ConfigureAwait(false);
        sut.MetallicFactor = 0.75f;
        await WaitForEditAsync(service, expectedCount: 1).ConfigureAwait(false);

        sut.Dispose();

        _ = service.ClosedDocuments.Should().BeEmpty();
    }

    /// <summary>Verifies pending edits finish before saving and closing the authoring document.</summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task ApprovedCloseWaitsForPendingEditAndUsesAuthoringDocumentIdentity()
    {
        var service = new RecordingDocumentService(CreateDocument());
        var editCompleted = new TaskCompletionSource<MaterialEditResult>(TaskCreationOptions.RunContinuationsAsynchronously);
        service.PendingEdit = editCompleted.Task;
        var metadata = new MaterialDocumentMetadata(service.Document.MaterialUri);
        using var sut = new MaterialEditorViewModel(metadata, service, Oxygen.Testing.AssetStatusFixture.EmptyProvider, System.Reactive.Concurrency.ImmediateScheduler.Instance);
        await WaitForLoadAsync(sut).ConfigureAwait(false);
        sut.MetallicFactor = 0.75f;

        var preparation = sut.PrepareForCloseAsync();
        _ = preparation.IsCompleted.Should().BeFalse();
        editCompleted.SetResult(new MaterialEditResult(Succeeded: true, OperationId: null));
        await preparation.ConfigureAwait(false);
        _ = metadata.IsDirty.Should().BeTrue();
        _ = (await sut.SaveForCloseAsync().ConfigureAwait(false)).Should().BeTrue();
        await sut.CloseAsync(discard: false).ConfigureAwait(false);

        _ = service.ClosedDocuments.Should().ContainSingle().Which.Should().Be(service.Document.DocumentId);
        _ = metadata.DocumentId.Should().NotBe(service.Document.DocumentId);
        _ = metadata.IsDirty.Should().BeFalse();
    }

    /// <summary>
    /// Verifies scalar edits use the schema-driven service entry point.
    /// </summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task ScalarPropertyChangesUseSchemaPropertyEdit()
    {
        var service = new RecordingDocumentService(CreateDocument());
        using var sut = new MaterialEditorViewModel(new MaterialDocumentMetadata(service.Document.MaterialUri), service, Oxygen.Testing.AssetStatusFixture.EmptyProvider, System.Reactive.Concurrency.ImmediateScheduler.Instance);
        await WaitForLoadAsync(sut).ConfigureAwait(false);

        sut.MetallicFactor = 0.75f;
        await WaitForEditAsync(service, expectedCount: 1).ConfigureAwait(false);

        _ = service.ScalarEditCalls.Should().Be(0);
        _ = service.PropertyEdits.Should().ContainSingle();
        _ = service.PropertyEdits[0].GetTyped(MaterialDescriptors.Metalness, out var value).Should().BeTrue();
        _ = value.Should().Be(0.75f);
        _ = sut.IsDirty.Should().BeTrue();
        _ = sut.CookStatusText.Should().Be("Unsaved changes");
    }

    /// <summary>
    /// Verifies the color picker commits all four base-color channels as one property edit.
    /// </summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task SetBaseColorBatchesChannelsIntoOneSchemaPropertyEdit()
    {
        var service = new RecordingDocumentService(CreateDocument());
        using var sut = new MaterialEditorViewModel(new MaterialDocumentMetadata(service.Document.MaterialUri), service, Oxygen.Testing.AssetStatusFixture.EmptyProvider, System.Reactive.Concurrency.ImmediateScheduler.Instance);
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
        => await WaitUntilAsync(() => viewModel.IsLoaded).ConfigureAwait(false);

    private static async Task WaitForEditAsync(RecordingDocumentService service, int expectedCount)
        => await WaitUntilAsync(() => service.PropertyEditCount >= expectedCount).ConfigureAwait(false);

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

        public Task<MaterialEditResult>? PendingEdit { get; set; }

        public List<Guid> ClosedDocuments { get; } = [];

        public MaterialDocument Document { get; private set; } = document;

        public Task? PendingWrite { get; set; }

        public TaskCompletionSource WriteStarted { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

        public List<PropertyEdit> PropertyEdits
        {
            get
            {
                lock (this.sync)
                {
                    return this.propertyEdits.ConvertAll(static edit => edit.Clone());
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

        public MaterialEditSession BeginEditSession(Guid documentId, string field) => new(documentId, Guid.NewGuid());

        public Task<MaterialEditResult> PreviewPropertiesAsync(MaterialEditSession session, PropertyEdit edit, CancellationToken cancellationToken = default)
            => this.EditPropertiesAsync(session.DocumentId, edit, cancellationToken);

        public MaterialEditResult CompleteEditSession(MaterialEditSession session, bool commit) => new(Succeeded: true, OperationId: null);

        public bool CanUndo(Guid documentId) => false;

        public bool CanRedo(Guid documentId) => false;

        public MaterialEditResult Undo(Guid documentId) => new(Succeeded: true, OperationId: null);

        public MaterialEditResult Redo(Guid documentId) => new(Succeeded: true, OperationId: null);

        public Task<MaterialDocument> ReloadAsync(Guid documentId, CancellationToken cancellationToken = default) => Task.FromResult(this.Document);

        public Task<Uri> SaveCopyAsync(Guid documentId, Uri targetUri, CancellationToken cancellationToken = default) => Task.FromResult(targetUri);

        public MaterialDocument GetDocument(Guid documentId) => this.Document;

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
                var state = new MaterialEditState(this.Document.Source);
                PropertyApply.ApplyToTarget(state, edit, MaterialDescriptors.Catalog.ById);
                this.Document = this.Document with { Source = state.Source, Revision = this.Document.Revision + 1, IsDirty = true, CookState = MaterialCookState.Stale };
            }

            return this.PendingEdit ?? Task.FromResult(new MaterialEditResult(Succeeded: true, OperationId: null));
        }

        public async Task<MaterialSaveResult> SaveAsync(Guid documentId, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var revision = this.Document.Revision;
            _ = this.WriteStarted.TrySetResult();
            if (this.PendingWrite is { } write)
            {
                await write.ConfigureAwait(false);
            }

            this.Document = this.Document with { SavedRevision = revision, IsDirty = this.Document.Revision != revision };
            return new MaterialSaveResult(Succeeded: true, OperationId: null) { HasUnsavedChanges = this.Document.IsDirty };
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
            this.ClosedDocuments.Add(documentId);
            _ = documentId;
            _ = discard;
            cancellationToken.ThrowIfCancellationRequested();
            return Task.CompletedTask;
        }
    }
}
