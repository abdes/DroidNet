// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Managed.Assets.Import;
using Oxygen.Managed.Assets.Import.Materials;
using Oxygen.Managed.Core.Diagnostics;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>
/// Tests material document authoring, schema-backed edits, persistence, and cooking behavior.
/// </summary>
[TestClass]
public sealed class MaterialDocumentServiceTests
{
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

        var created = await service.CreateAsync(materialUri).ConfigureAwait(false);

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

        var opened = await service.OpenAsync(materialUri).ConfigureAwait(false);
        _ = opened.DisplayName.Should().Be("Gold");
        _ = opened.Source.Name.Should().Be("Gold");

        var save = await service.SaveAsync(opened.DocumentId).ConfigureAwait(false);

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
        var service = new MaterialDocumentService(new TestResolver(workspace.Root), cook);

        var created = await service.CreateAsync(materialUri).ConfigureAwait(false);
        var result = await service.CookAsync(created.DocumentId).ConfigureAwait(false);

        _ = result.State.Should().Be(MaterialCookState.Cooked);
        _ = cook.LastRequest.Should().NotBeNull();
        _ = cook.LastRequest!.SourceRelativePath.Should().Be("Content/Materials/Test.omat.json");
    }

    /// <summary>
    /// Verifies dirty documents are rejected before the cook service is called.
    /// </summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task CookAsyncRejectsDirtyDocumentWithoutCallingCookService()
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
        var created = await service.CreateAsync(materialUri).ConfigureAwait(false);
        var edit = new PropertyEdit();
        edit.Set(MaterialDescriptors.Metalness, 0.8f);
        edit.Set(MaterialDescriptors.Roughness, 0.25f);
        edit.Set(MaterialDescriptors.DoubleSided, true);

        var result = await ((IMaterialPropertyEditService)service).EditPropertiesAsync(created.DocumentId, edit).ConfigureAwait(false);
        var save = await service.SaveAsync(created.DocumentId).ConfigureAwait(false);
        await service.CloseAsync(created.DocumentId, discard: false).ConfigureAwait(false);
        var reopened = await service.OpenAsync(materialUri).ConfigureAwait(false);

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
        var created = await service.CreateAsync(materialUri).ConfigureAwait(false);

        var result = await ((IMaterialPropertyEditService)service).EditPropertiesAsync(
            created.DocumentId,
            PropertyEdit.Single(MaterialDescriptors.AlphaMode, MaterialAlphaMode.Mask)).ConfigureAwait(false);
        var save = await service.SaveAsync(created.DocumentId).ConfigureAwait(false);
        await service.CloseAsync(created.DocumentId, discard: false).ConfigureAwait(false);
        var reopened = await service.OpenAsync(materialUri).ConfigureAwait(false);

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
        var service = new MaterialDocumentService(new TestResolver(workspace.Root), new RecordingCookService(), publisher);
        var created = await service.CreateAsync(materialUri).ConfigureAwait(false);
        var edit = PropertyEdit.Single(MaterialDescriptors.Metalness, 2.0f);

        var result = await ((IMaterialPropertyEditService)service).EditPropertiesAsync(created.DocumentId, edit).ConfigureAwait(false);
        var save = await service.SaveAsync(created.DocumentId).ConfigureAwait(false);
        await service.CloseAsync(created.DocumentId, discard: false).ConfigureAwait(false);
        var reopened = await service.OpenAsync(materialUri).ConfigureAwait(false);

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
        var service = new MaterialDocumentService(new TestResolver(workspace.Root), cook);
        var created = await service.CreateAsync(materialUri).ConfigureAwait(false);

        var result = await ((IMaterialPropertyEditService)service).EditPropertiesAsync(
            created.DocumentId,
            PropertyEdit.Single(MaterialDescriptors.Metalness, created.Source.PbrMetallicRoughness.MetallicFactor)).ConfigureAwait(false);
        var cookResult = await service.CookAsync(created.DocumentId).ConfigureAwait(false);

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
        var service = new MaterialDocumentService(new TestResolver(workspace.Root), new RecordingCookService(), publisher);
        var created = await service.CreateAsync(materialUri).ConfigureAwait(false);
        var unknown = new PropertyId<float>("material", "/parameters/unknown");

        var result = await ((IMaterialPropertyEditService)service).EditPropertiesAsync(
            created.DocumentId,
            PropertyEdit.Single(unknown, 0.25f)).ConfigureAwait(false);
        var save = await service.SaveAsync(created.DocumentId).ConfigureAwait(false);
        await service.CloseAsync(created.DocumentId, discard: false).ConfigureAwait(false);
        var reopened = await service.OpenAsync(materialUri).ConfigureAwait(false);

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
        var created = await service.CreateAsync(materialUri).ConfigureAwait(false);
        var edit = new PropertyEdit();
        edit.Set(MaterialDescriptors.BaseColorR, 0.25f);
        edit.Set(MaterialDescriptors.BaseColorG, 0.5f);
        edit.Set(MaterialDescriptors.BaseColorB, 0.75f);
        edit.Set(MaterialDescriptors.BaseColorA, 1.0f);
        edit.Set(MaterialDescriptors.Metalness, 0.8f);
        edit.Set(MaterialDescriptors.Roughness, 0.2f);
        edit.Set(MaterialDescriptors.AlphaMode, MaterialAlphaMode.Mask);
        edit.Set(MaterialDescriptors.AlphaCutoff, 0.4f);
        edit.Set(MaterialDescriptors.DoubleSided, true);

        var editResult = await service.EditPropertiesAsync(created.DocumentId, edit).ConfigureAwait(false);
        var save = await service.SaveAsync(created.DocumentId).ConfigureAwait(false);
        var cook = await service.CookAsync(created.DocumentId).ConfigureAwait(false);

        _ = editResult.Succeeded.Should().BeTrue();
        _ = save.Succeeded.Should().BeTrue();
        _ = cook.State.Should().Be(MaterialCookState.Cooked);

        var cookedBytes = await File.ReadAllBytesAsync(
            Path.Combine(workspace.Root, ".cooked", "Content", "Materials", "RoundTrip.omat")).ConfigureAwait(false);
        _ = cookedBytes.Should().HaveCount(256);
        _ = ReadSingle(cookedBytes, 0x68).Should().BeApproximately(0.25f, 0.0001f);
        _ = ReadSingle(cookedBytes, 0x6C).Should().BeApproximately(0.5f, 0.0001f);
        _ = ReadSingle(cookedBytes, 0x70).Should().BeApproximately(0.75f, 0.0001f);
        _ = ReadSingle(cookedBytes, 0x74).Should().BeApproximately(1.0f, 0.0001f);
        _ = ReadUnorm16(cookedBytes, 0x7C).Should().BeApproximately(0.8f, 0.0001f);
        _ = ReadUnorm16(cookedBytes, 0x7E).Should().BeApproximately(0.2f, 0.0001f);
        _ = ReadUnorm16(cookedBytes, 0xB8).Should().BeApproximately(0.4f, 0.0001f);
        _ = cookedBytes[0x5F].Should().Be(3);

        var flags = BinaryPrimitives.ReadUInt32LittleEndian(cookedBytes.AsSpan(0x60, 4));
        _ = flags.Should().Be((1u << 1) | (1u << 2));
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
            if (segment.All(static ch => ch is >= '0' and <= '9') && node["items"] is JsonObject)
            {
                return coveredPointer;
            }

            return null;
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
        using var stream = File.Create(path);
        MaterialSourceWriter.Write(stream, source);
        await stream.FlushAsync().ConfigureAwait(false);
    }

    private static MaterialDocumentService CreateService(TempWorkspace workspace)
        => new(new TestResolver(workspace.Root), new RecordingCookService());

    private static MaterialDocumentService CreateCookingService(TempWorkspace workspace)
    {
        var registry = new ImporterRegistry();
        registry.Register(new MaterialSourceImporter());
        return new MaterialDocumentService(
            new TestResolver(workspace.Root),
            new MaterialCookService(new ImportService(registry), NullLogger<MaterialCookService>.Instance));
    }

    private static float ReadSingle(byte[] bytes, int offset)
    {
        var bits = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(offset, 4));
        return BitConverter.Int32BitsToSingle((int)bits);
    }

    private static float ReadUnorm16(byte[] bytes, int offset)
        => BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(offset, 2)) / 65535.0f;

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
