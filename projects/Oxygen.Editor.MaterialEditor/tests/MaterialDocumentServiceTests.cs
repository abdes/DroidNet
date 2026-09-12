// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Import.Materials;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>
/// Tests material document authoring, schema-backed edits, persistence, and cooking behavior.
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers these public test classes using the repository's default discovery configuration.")]
public sealed partial class MaterialDocumentServiceTests
{
    /// <summary>Gets or sets the current test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Verifies an I/O failure retains dirty state and allows a successful retry.</summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task SaveIoFailurePublishesResultAndKeepsMaterialDirtyAndOpen()
    {
        using var workspace = new TempWorkspace();
        var results = new RecordingOperationPublisher();
        var service = new MaterialDocumentService(Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(), new TestResolver(workspace.Root), new RecordingCookService(), workspace.CookDocuments, CreateFileStore(), results);
        var document = await service.CreateAsync(new Uri("asset:///Content/Materials/Locked.omat.json"), cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await service.EditScalarAsync(document.DocumentId, new MaterialFieldEdit(MaterialFieldKeys.MetallicFactor, 0.75f), cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        await using (var lockedFile = new FileStream(document.SourcePath, FileMode.Open, FileAccess.Read, FileShare.None).ConfigureAwait(false))
        {
            var result = await service.SaveAsync(document.DocumentId, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = result.Succeeded.Should().BeFalse();
            _ = results.Published.Should().ContainSingle().Which.Status.Should().Be(OperationStatus.Failed);
            Func<Task> close = () => service.CloseAsync(document.DocumentId, discard: false, cancellationToken: this.TestContext.CancellationToken);
            _ = await close.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
        }

        _ = (await service.SaveAsync(document.DocumentId, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        await service.CloseAsync(document.DocumentId, discard: false, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var reopened = await service.OpenAsync(document.MaterialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = reopened.Source.PbrMetallicRoughness.MetallicFactor.Should().Be(0.75f);
    }

    /// <summary>Verifies explicit discard preserves the last persisted authoring values.</summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task ExplicitDiscardReopensLastSavedMaterial()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var document = await service.CreateAsync(new Uri("asset:///Content/Materials/Discard.omat.json"), cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await service.EditScalarAsync(document.DocumentId, new MaterialFieldEdit(MaterialFieldKeys.MetallicFactor, 0.75f), cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        await service.CloseAsync(document.DocumentId, discard: true, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        var reopened = await service.OpenAsync(document.MaterialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = reopened.Source.PbrMetallicRoughness.MetallicFactor.Should().Be(0.0f);
    }

    /// <summary>
    /// Verifies scalar material edits survive save, close, and reopen.
    /// </summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task CreateEditSaveOpenRoundTripsScalarFields()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Test.omat.json");
        var service = CreateService(workspace);

        var created = await service.CreateAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var edit = await service.EditScalarAsync(
            created.DocumentId,
            new MaterialFieldEdit(MaterialFieldKeys.MetallicFactor, 0.75f),
            cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = edit.Succeeded.Should().BeTrue();
        var save = await service.SaveAsync(created.DocumentId, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = save.Succeeded.Should().BeTrue();

        await service.CloseAsync(created.DocumentId, discard: false, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var reopened = await service.OpenAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = reopened.MaterialGuid.Should().Be(created.MaterialGuid);
        _ = reopened.Source.Schema.Should().Be("oxygen.material.v1");
        _ = reopened.Source.PbrMetallicRoughness.MetallicFactor.Should().Be(0.75f);
        _ = reopened.Source.PbrMetallicRoughness.RoughnessFactor.Should().Be(0.5f);
    }

    /// <summary>
    /// Verifies newly created materials use the asset file stem as the authored material name.
    /// </summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task CreateAsyncUsesAssetFileStemAsMaterialName()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Gold.omat.json");
        var service = CreateService(workspace);

        var created = await service.CreateAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = created.DisplayName.Should().Be("Gold");
        _ = created.MaterialGuid.Should().NotBe(Guid.Empty);
        _ = created.Source.Name.Should().Be("Gold");
        _ = ReadMaterial(workspace, "Content/Materials/Gold.omat.json").Name.Should().Be("Gold");
    }

    /// <summary>
    /// Verifies saving normalizes descriptor names to the canonical asset file stem.
    /// </summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task SaveAsyncPersistsFileStemNameWhenDescriptorNameDiffersFromAssetFileStem()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Gold.omat.json");
        await WriteMaterialAsync(workspace, "Content/Materials/Gold.omat.json", "Old Descriptor Name").ConfigureAwait(false);
        var service = CreateService(workspace);

        var opened = await service.OpenAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = opened.DisplayName.Should().Be("Gold");
        _ = opened.Source.Name.Should().Be("Gold");

        var save = await service.SaveAsync(opened.DocumentId, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = save.Succeeded.Should().BeTrue();
        _ = ReadMaterial(workspace, "Content/Materials/Gold.omat.json").Name.Should().Be("Gold");
    }

    /// <summary>
    /// Verifies legacy scalar editing clamps fields to supported ranges.
    /// </summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task EditScalarAsyncClampsOutOfRangeScalarFields()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Test.omat.json");
        var service = CreateService(workspace);

        var created = await service.CreateAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var edit = await service.EditScalarAsync(
            created.DocumentId,
            new MaterialFieldEdit(MaterialFieldKeys.BaseColorR, 2.0f),
            cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = edit.Succeeded.Should().BeTrue();

        edit = await service.EditScalarAsync(
            created.DocumentId,
            new MaterialFieldEdit(MaterialFieldKeys.RoughnessFactor, -1.0f),
            cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = edit.Succeeded.Should().BeTrue();

        _ = await service.SaveAsync(created.DocumentId, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        await service.CloseAsync(created.DocumentId, discard: false, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        var reopened = await service.OpenAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = reopened.Source.PbrMetallicRoughness.BaseColorR.Should().Be(1.0f);
        _ = reopened.Source.PbrMetallicRoughness.RoughnessFactor.Should().Be(0.0f);
    }

    /// <summary>
    /// Verifies cooking delegates to the cook service and updates material cook state.
    /// </summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task CookAsyncUsesMaterialCookServiceAndUpdatesState()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Test.omat.json");
        var cook = new RecordingCookService();
        var service = new MaterialDocumentService(Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(), new TestResolver(workspace.Root), cook, workspace.CookDocuments, CreateFileStore());

        var created = await service.CreateAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var result = await service.CookAsync(created.DocumentId, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.State.Should().Be(MaterialCookState.Cooked);
        _ = cook.LastRequest.Should().NotBeNull();
        _ = cook.LastRequest!.SourceRelativePath.Should().Be("Content/Materials/Test.omat.json");
    }

    /// <summary>
    /// Verifies dirty documents remain visible as blocked cooks until explicitly saved.
    /// </summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task CookAsyncWaitsForExplicitSaveThenResumesLatestSource()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Test.omat.json");
        var service = CreateCookingService(workspace);
        var blocked = new TaskCompletionSource<Oxygen.Editor.ContentPipeline.Cooking.CookRunSnapshot>(TaskCreationOptions.RunContinuationsAsynchronously);
        workspace.CookCoordinator.RunChanged += (_, args) =>
        {
            if (args.Run.State == Oxygen.Editor.ContentPipeline.Cooking.CookRunState.NeedsSave)
            {
                _ = blocked.TrySetResult(args.Run);
            }
        };

        var created = await service.CreateAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await service.EditScalarAsync(
            created.DocumentId,
            new MaterialFieldEdit(MaterialFieldKeys.MetallicFactor, 0.25f),
            cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        var cooking = service.CookAsync(created.DocumentId, cancellationToken: this.TestContext.CancellationToken);
        var run = await blocked.Task.WaitAsync(TimeSpan.FromSeconds(10), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = run.UnsavedDocuments.Single().DocumentId.Should().Be(created.DocumentId);
        _ = cooking.IsCompleted.Should().BeFalse();
        _ = service.GetDocument(created.DocumentId).IsDirty.Should().BeTrue();
        _ = (await service.SaveAsync(created.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = workspace.CookCoordinator.ResumeAfterSave(run.OperationId).Should().BeTrue();
        var result = await cooking.WaitAsync(TimeSpan.FromSeconds(10), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.State.Should().Be(MaterialCookState.Cooked);
        _ = result.OperationId.Should().Be(run.OperationId);
    }

    /// <summary>
    /// Verifies descriptor-backed material edits mutate the source and mark cooking stale.
    /// </summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task EditPropertiesAsyncAppliesDescriptorBackedMaterialEditAndMarksDocumentStale()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Test.omat.json");
        var service = CreateService(workspace);
        var created = await service.CreateAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var edit = new PropertyEdit();
        edit.Set(MaterialDescriptors.Metalness, 0.8f);
        edit.Set(MaterialDescriptors.Roughness, 0.25f);
        edit.Set(MaterialDescriptors.DoubleSided, value: true);

        var result = await ((IMaterialPropertyEditService)service).EditPropertiesAsync(created.DocumentId, edit, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var save = await service.SaveAsync(created.DocumentId, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        await service.CloseAsync(created.DocumentId, discard: false, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var reopened = await service.OpenAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = save.Succeeded.Should().BeTrue();
        _ = reopened.Source.PbrMetallicRoughness.MetallicFactor.Should().Be(0.8f);
        _ = reopened.Source.PbrMetallicRoughness.RoughnessFactor.Should().Be(0.25f);
        _ = reopened.Source.DoubleSided.Should().BeTrue();
    }

    /// <summary>
    /// Verifies alpha mode is editable through the material descriptor catalog.
    /// </summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task EditPropertiesAsyncAppliesAlphaModeThroughDescriptor()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Test.omat.json");
        var service = CreateService(workspace);
        var created = await service.CreateAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        var result = await ((IMaterialPropertyEditService)service).EditPropertiesAsync(
            created.DocumentId,
            PropertyEdit.Single(MaterialDescriptors.AlphaMode, MaterialAlphaMode.Mask),
            cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var save = await service.SaveAsync(created.DocumentId, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        await service.CloseAsync(created.DocumentId, discard: false, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var reopened = await service.OpenAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = save.Succeeded.Should().BeTrue();
        _ = reopened.Source.AlphaMode.Should().Be(MaterialAlphaMode.Mask);
    }

    /// <summary>
    /// Verifies descriptor validation rejects out-of-range scalar values without mutating the source.
    /// </summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task EditPropertiesAsyncRejectsOutOfRangeScalarWithoutMutating()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Test.omat.json");
        var publisher = new RecordingOperationPublisher();
        var service = new MaterialDocumentService(Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(), new TestResolver(workspace.Root), new RecordingCookService(), workspace.CookDocuments, CreateFileStore(), publisher);
        var created = await service.CreateAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var edit = PropertyEdit.Single(MaterialDescriptors.Metalness, 2.0f);

        var result = await ((IMaterialPropertyEditService)service).EditPropertiesAsync(created.DocumentId, edit, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var save = await service.SaveAsync(created.DocumentId, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        await service.CloseAsync(created.DocumentId, discard: false, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var reopened = await service.OpenAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = save.Succeeded.Should().BeTrue();
        _ = reopened.Source.PbrMetallicRoughness.MetallicFactor.Should().Be(0.0f);
        _ = publisher.Published.Should().ContainSingle(r =>
            r.Diagnostics.Single().Code == "PROPERTY_OUT_OF_RANGE"
            && r.Diagnostics.Single().Domain == FailureDomain.MaterialAuthoring);
    }

    /// <summary>
    /// Verifies unchanged descriptor edits do not dirty the material or mark cooking stale.
    /// </summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task EditPropertiesAsyncDoesNotDirtyOrMarkCookStaleWhenValueIsUnchanged()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Test.omat.json");
        var cook = new RecordingCookService();
        var service = new MaterialDocumentService(Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(), new TestResolver(workspace.Root), cook, workspace.CookDocuments, CreateFileStore());
        var created = await service.CreateAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        var result = await ((IMaterialPropertyEditService)service).EditPropertiesAsync(
            created.DocumentId,
            PropertyEdit.Single(MaterialDescriptors.Metalness, created.Source.PbrMetallicRoughness.MetallicFactor),
            cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var cookResult = await service.CookAsync(created.DocumentId, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = cookResult.State.Should().Be(MaterialCookState.Cooked);
        _ = cook.LastRequest.Should().NotBeNull();
    }

    /// <summary>
    /// Verifies unknown material property ids are rejected without mutating the source.
    /// </summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task EditPropertiesAsyncRejectsUnknownPropertyIdWithoutMutating()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Test.omat.json");
        var publisher = new RecordingOperationPublisher();
        var service = new MaterialDocumentService(Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(), new TestResolver(workspace.Root), new RecordingCookService(), workspace.CookDocuments, CreateFileStore(), publisher);
        var created = await service.CreateAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var unknown = new PropertyId<float>("material", "/parameters/unknown");

        var result = await ((IMaterialPropertyEditService)service).EditPropertiesAsync(
            created.DocumentId,
            PropertyEdit.Single(unknown, 0.25f),
            cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var save = await service.SaveAsync(created.DocumentId, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        await service.CloseAsync(created.DocumentId, discard: false, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var reopened = await service.OpenAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = save.Succeeded.Should().BeTrue();
        _ = reopened.Source.PbrMetallicRoughness.MetallicFactor.Should().Be(0.0f);
        _ = publisher.Published.Should().ContainSingle(r =>
            r.Diagnostics.Single().Code == "PROPERTY_UNKNOWN"
            && r.Diagnostics.Single().Domain == FailureDomain.MaterialAuthoring);
    }

    /// <summary>
    /// Verifies a schema-driven material edit round-trips through save and cook into the cooked descriptor layout.
    /// </summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task EditPropertiesSaveCookRoundTripsSchemaValuesIntoCookedDescriptor()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/RoundTrip.omat.json");
        var service = CreateCookingService(workspace);
        var created = await service.CreateAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var edit = new PropertyEdit();
        edit.Set(MaterialDescriptors.BaseColorR, 0.25f);
        edit.Set(MaterialDescriptors.BaseColorG, 0.5f);
        edit.Set(MaterialDescriptors.BaseColorB, 0.75f);
        edit.Set(MaterialDescriptors.BaseColorA, 1.0f);
        edit.Set(MaterialDescriptors.Metalness, 0.8f);
        edit.Set(MaterialDescriptors.Roughness, 0.2f);
        edit.Set(MaterialDescriptors.AlphaMode, MaterialAlphaMode.Mask);
        edit.Set(MaterialDescriptors.AlphaCutoff, 0.4f);
        edit.Set(MaterialDescriptors.DoubleSided, value: true);

        var editResult = await service.EditPropertiesAsync(created.DocumentId, edit, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var save = await service.SaveAsync(created.DocumentId, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var cook = await service.CookAsync(created.DocumentId, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = editResult.Succeeded.Should().BeTrue();
        _ = save.Succeeded.Should().BeTrue();
        _ = cook.State.Should().Be(MaterialCookState.Cooked);

        var cookedBytes = await File.ReadAllBytesAsync(
            Path.Combine(workspace.Root, ".cooked", "Content", "Materials", "RoundTrip.omat"), cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var indexStream = File.OpenRead(Path.Combine(workspace.Root, ".cooked", "Content", "container.index.bin"));
        await using var indexLifetime = indexStream.ConfigureAwait(false);
        var index = Oxygen.Managed.Assets.Persistence.LooseCooked.V1.LooseCookedIndex.Read(indexStream);
        var materialEntry = index.Assets.Single(static asset => string.Equals(asset.VirtualPath, "/Content/Materials/RoundTrip.omat", StringComparison.Ordinal));
        _ = cookedBytes.Should().HaveCount(checked((int)materialEntry.DescriptorSize));
        _ = ReadSingle(cookedBytes, 0x70).Should().BeApproximately(0.25f, 0.0001f);
        _ = ReadSingle(cookedBytes, 0x74).Should().BeApproximately(0.5f, 0.0001f);
        _ = ReadSingle(cookedBytes, 0x78).Should().BeApproximately(0.75f, 0.0001f);
        _ = ReadSingle(cookedBytes, 0x7C).Should().BeApproximately(1.0f, 0.0001f);
        _ = ReadUnorm16(cookedBytes, 0x84).Should().BeApproximately(0.8f, 0.0001f);
        _ = ReadUnorm16(cookedBytes, 0x86).Should().BeApproximately(0.2f, 0.0001f);
        _ = ReadUnorm16(cookedBytes, 0xC0).Should().BeApproximately(0.4f, 0.0001f);
        _ = cookedBytes[0x67].Should().Be(3);

        var flags = BinaryPrimitives.ReadUInt32LittleEndian(cookedBytes.AsSpan(0x68, 4));
        const uint authoredFlags = (1u << 1) | (1u << 2);
        _ = (flags & authoredFlags).Should().Be(authoredFlags);
        _ = (flags & 1u).Should().Be(1u, "the native scalar material cook disables texture sampling");
    }

    /// <summary>
    /// Verifies engine-schema validation and merged-overlay validation accept and reject the same material samples.
    /// </summary>
    [TestMethod]
    public void MaterialSchemaValidatorKeepsEngineAndMergedOverlayAcceptanceEquivalent()
    {
        var schemaRoot = FindSchemaRoot();
        var catalog = EditorSchemaCatalog.LoadFromDirectory(schemaRoot);
        var validator = new MaterialSchemaValidator(catalog);
        var source = new MaterialSource(
            schema: "oxygen.material.v1",
            type: "PBR",
            name: "Gold",
            pbrMetallicRoughness: new MaterialPbrMetallicRoughness(
                baseColorR: 1.0f,
                baseColorG: 0.8f,
                baseColorB: 0.2f,
                baseColorA: 1.0f,
                metallicFactor: 1.0f,
                roughnessFactor: 0.35f,
                baseColorTexture: null,
                metallicRoughnessTexture: null),
            normalTexture: null,
            occlusionTexture: null,
            alphaMode: MaterialAlphaMode.Opaque,
            alphaCutoff: 0.5f,
            doubleSided: false);
        var valid = MaterialSourceProjection.ToEngineJson(source);
        var invalid = MaterialSourceProjection.ToEngineJson(source);
        invalid["parameters"]!.AsObject()["metalness"] = 2.0;

        _ = validator.ValidatorParityHolds(valid).Should().BeTrue();
        _ = validator.ValidatorParityHolds(invalid).Should().BeTrue();
        _ = validator.ValidateAgainstEngineSchema(valid).IsValid.Should().BeTrue();
        _ = validator.ValidateAgainstEngineSchema(invalid).IsValid.Should().BeFalse();
        _ = validator.LintOverlay().Should().BeEmpty();
    }

    /// <summary>
    /// Verifies material descriptors map to engine schema paths annotated by the material overlay.
    /// </summary>
    [TestMethod]
    public void MaterialDescriptorsMapToOverlayAnnotatedEngineSchemaPaths()
    {
        var schemaRoot = FindSchemaRoot();
        var engine = LoadSchema(schemaRoot, MaterialSchemaValidator.EngineSchemaFileName);
        var overlay = LoadSchema(schemaRoot, MaterialSchemaValidator.EditorOverlayFileName);
        var annotations = EditorSchemaOverlay.ExtractAnnotations(overlay);

        foreach (var descriptor in MaterialDescriptors.Catalog.ById.Values)
        {
            var schemaPath = ResolveSchemaCoveragePath(engine, descriptor.Id.Pointer);
            _ = schemaPath.Should().NotBeNull($"descriptor {descriptor.Id} must point at the engine material schema");

            var annotationPath = ResolveAnnotationPath(annotations, descriptor.Id.Pointer);
            _ = annotationPath.Should().NotBeNull($"descriptor {descriptor.Id} must be backed by the material editor overlay");
            var annotation = annotations[annotationPath!];
            _ = annotation.Renderer.Should().Be(descriptor.Annotation.Renderer, $"descriptor {descriptor.Id} should use the overlay renderer");
            _ = annotation.Label.Should().NotBeNullOrWhiteSpace($"descriptor {descriptor.Id} should inherit a visible overlay label");
        }
    }

    /// <summary>
    /// Verifies legacy name edits are rejected because the asset file stem owns the material name.
    /// </summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task EditScalarAsyncRejectsNameEditsBecauseAssetFileStemIsCanonical()
    {
        using var workspace = new TempWorkspace();
        var materialUri = new Uri("asset:///Content/Materials/Test.omat.json");
        var publisher = new RecordingOperationPublisher();
        var service = new MaterialDocumentService(Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(), new TestResolver(workspace.Root), new RecordingCookService(), workspace.CookDocuments, CreateFileStore(), publisher);

        var created = await service.CreateAsync(materialUri, cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);
        var result = await service.EditScalarAsync(
            created.DocumentId,
            new MaterialFieldEdit(MaterialFieldKeys.Name, "Renamed"),
            cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.OperationId.Should().NotBeNull();
        _ = publisher.Published.Should().ContainSingle(r =>
            r.OperationKind == MaterialOperationKinds.EditScalar
            && r.Diagnostics.Single().Domain == FailureDomain.MaterialAuthoring
            && r.Diagnostics.Single().Code == MaterialDiagnosticCodes.FieldRejected);
    }

    /// <summary>
    /// Verifies local-folder mounted material URIs resolve to the mounted source path.
    /// </summary>
    [TestMethod]
    public void ProjectMaterialSourcePathResolverResolvesLocalFolderMountMaterialUri()
    {
        using var workspace = new TempWorkspace();
        var contextService = new ProjectContextService();
        contextService.Activate(
            new ProjectContext
            {
                ProjectId = Guid.NewGuid(),
                Name = "Project",
                Category = Category.Games,
                ProjectRoot = @"C:\Project",
                AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
                LocalFolderMounts = [new LocalFolderMount("StudioLibrary", workspace.Root)],
                Scenes = [],
            });
        var resolver = new ProjectMaterialSourcePathResolver(contextService);

        var location = resolver.Resolve(new Uri("asset:///StudioLibrary/Materials/Shared.omat.json"));

        _ = location.MountName.Should().Be("StudioLibrary");
        _ = location.SourcePath.Should().Be(Path.Combine(workspace.Root, "Materials", "Shared.omat.json"));
        _ = location.SourceRelativePath.Should().Be("Materials/Shared.omat.json");
    }

    private static string FindSchemaRoot()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);
        while (current is not null)
        {
            var candidate = Path.Combine(
                current.FullName,
                "projects",
                "Oxygen.Engine",
                "src",
                "Oxygen",
                "Cooker",
                "Import",
                "Schemas");
            if (Directory.Exists(candidate))
            {
                return candidate;
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException("Could not find Oxygen.Engine cooker schema directory.");
    }

    private static JsonObject LoadSchema(string schemaRoot, string fileName)
        => JsonNode.Parse(File.ReadAllText(Path.Combine(schemaRoot, fileName)))!.AsObject();

    private static string? ResolveSchemaCoveragePath(JsonObject root, string pointer)
    {
        var node = root;
        var coveredPointer = string.Empty;
        foreach (var segment in DecodePointer(pointer))
        {
            node = ResolveLocalReference(root, node);
            if (node["properties"] is JsonObject properties
                && properties[segment] is JsonObject propertyNode)
            {
                coveredPointer += "/" + EncodePointerSegment(segment);
                node = propertyNode;
                continue;
            }

            node = ResolveLocalReference(root, node);
            return segment.All(static ch => ch is >= '0' and <= '9') && node["items"] is JsonObject
                ? coveredPointer
                : null;
        }

        return coveredPointer;
    }

    private static string? ResolveAnnotationPath(
        IReadOnlyDictionary<string, EditorAnnotation> annotations,
        string pointer)
    {
        var current = pointer;
        while (current.Length > 0)
        {
            if (annotations.ContainsKey(current))
            {
                return current;
            }

            var slash = current.LastIndexOf('/');
            if (slash <= 0)
            {
                break;
            }

            current = current[..slash];
        }

        return null;
    }

    private static IEnumerable<string> DecodePointer(string pointer)
        => pointer.TrimStart('/')
            .Split('/', StringSplitOptions.RemoveEmptyEntries)
            .Select(static segment => segment.Replace("~1", "/", StringComparison.Ordinal).Replace("~0", "~", StringComparison.Ordinal));

    private static string EncodePointerSegment(string segment)
        => segment.Replace("~", "~0", StringComparison.Ordinal).Replace("/", "~1", StringComparison.Ordinal);

    private static JsonObject ResolveLocalReference(JsonObject root, JsonObject node)
    {
        if (node["$ref"]?.GetValue<string>() is not { } reference
            || !reference.StartsWith("#/definitions/", StringComparison.Ordinal)
            || root["definitions"] is not JsonObject definitions)
        {
            return node;
        }

        var definitionName = reference["#/definitions/".Length..].Replace("~1", "/", StringComparison.Ordinal).Replace("~0", "~", StringComparison.Ordinal);
        return definitions[definitionName] is JsonObject definition ? definition : node;
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
        var stream = File.Create(path);
        await using (stream.ConfigureAwait(false))
        {
            MaterialSourceWriter.Write(stream, source);
            await stream.FlushAsync().ConfigureAwait(false);
        }
    }

    private static NativeAtomicFileStore CreateFileStore()
        => new(new Testably.Abstractions.RealFileSystem());

    private static MaterialDocumentService CreateService(TempWorkspace workspace)
        => new(Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(), new TestResolver(workspace.Root), new RecordingCookService(), workspace.CookDocuments, CreateFileStore());

    private static MaterialDocumentService CreateCookingService(TempWorkspace workspace)
        => new(
            Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(),
            new TestResolver(workspace.Root),
            new MaterialCookService(workspace.NativePipeline.Pipeline, workspace.ContextService, NullLogger<MaterialCookService>.Instance),
            workspace.CookDocuments,
            CreateFileStore());

    private static float ReadSingle(byte[] bytes, int offset)
    {
        var bits = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(offset, 4));
        return BitConverter.Int32BitsToSingle((int)bits);
    }

    private static float ReadUnorm16(byte[] bytes, int offset)
        => BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(offset, 2)) / 65535.0f;

    private sealed class RecordingCookService : IMaterialCookService
    {
        public Task<MaterialCookResult>? Completion { get; init; }

        public MaterialCookRequest? LastRequest { get; private set; }

        public Task<MaterialCookResult> CookMaterialAsync(MaterialCookRequest request, CancellationToken cancellationToken = default)
        {
            this.LastRequest = request;
            return this.Completion ?? Task.FromResult(
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

    private sealed partial class EmptyDisposable : IDisposable
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

    private sealed partial class TempWorkspace : IDisposable
    {
        private Oxygen.Testing.NativeContentPipelineFixture? nativePipeline;

        public TempWorkspace()
        {
            this.Root = Path.Combine(Path.GetTempPath(), "oxygen-material-editor-tests", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(this.Root);
            this.ContextService.Activate(ProjectContext.FromProjectInfo(new ProjectInfo("Material tests", Category.Games, this.Root)
            {
                AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
            }));
            this.CookCoordinator = new ContentCookCoordinator(this.ContextService, NullLogger<ContentCookCoordinator>.Instance);
        }

        public string Root { get; }

        public ProjectContextService ContextService { get; } = new();

        public ContentCookCoordinator CookCoordinator { get; }

        public Oxygen.Testing.NativeContentPipelineFixture NativePipeline => this.nativePipeline ??= new(this.ContextService, this.CookCoordinator, this.CookDocuments);

        public Oxygen.Editor.ContentPipeline.Snapshots.CookDocumentRegistry CookDocuments { get; } = new();

        public void Dispose()
        {
            this.ContextService.Close();
            this.CookCoordinator.Dispose();
            this.nativePipeline?.Dispose();
            if (Directory.Exists(this.Root))
            {
                Directory.Delete(this.Root, recursive: true);
            }
        }
    }
}
