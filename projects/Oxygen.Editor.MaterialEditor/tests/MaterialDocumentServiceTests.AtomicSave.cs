// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Verifies external-change rejection without losing either version.</summary>
public sealed partial class MaterialDocumentServiceTests
{
    /// <summary>Rejects an external material change and preserves the unsaved authoring source.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ExternalMaterialChangeReturnsConflictAndPreservesBothVersions()
    {
        using var workspace = new TempWorkspace();
        var results = new RecordingOperationPublisher();
        var service = new MaterialDocumentService(new TestResolver(workspace.Root), new RecordingCookService(), workspace.CookDocuments, CreateFileStore(), results);
        var document = await service.CreateAsync(new Uri("asset:///Content/Materials/Conflict.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await service.EditScalarAsync(document.DocumentId, new(MaterialFieldKeys.RoughnessFactor, 0.8f), this.TestContext.CancellationToken).ConfigureAwait(false);
        var external = await File.ReadAllTextAsync(document.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false) + "\n ";
        await File.WriteAllTextAsync(document.SourcePath, external, this.TestContext.CancellationToken).ConfigureAwait(false);

        var saved = await service.SaveAsync(document.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = saved.Succeeded.Should().BeFalse();
        _ = saved.IsConflict.Should().BeTrue();
        _ = service.GetDocument(document.DocumentId).IsDirty.Should().BeTrue();
        _ = service.GetDocument(document.DocumentId).Source.PbrMetallicRoughness.RoughnessFactor.Should().Be(0.8f);
        _ = (await File.ReadAllTextAsync(document.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(external);
        _ = results.Published.Should().Contain(result => result.Diagnostics.Any(diagnostic => string.Equals(diagnostic.Code, DiagnosticCodes.DocumentPrefix + "Conflict", StringComparison.Ordinal)));
    }

    /// <summary>A copy gets a distinct asset identity and cannot replace an existing target.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task SaveCopyPreservesOriginalHistoryAndUsesDistinctIdentity()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var original = await service.CreateAsync(new Uri("asset:///Content/Materials/Original.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await service.EditPropertiesAsync(original.DocumentId, PropertyEdit.Single(MaterialDescriptors.Roughness, 0.8f), this.TestContext.CancellationToken).ConfigureAwait(false);
        var copyUri = await service.SaveCopyAsync(original.DocumentId, new Uri("asset:///Content/Materials/Copy.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var copy = await service.OpenAsync(copyUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = copy.MaterialGuid.Should().NotBe(original.MaterialGuid);
        _ = copy.Source.Name.Should().Be("Copy");
        _ = copy.Source.PbrMetallicRoughness.RoughnessFactor.Should().Be(0.8f);
        _ = service.GetDocument(original.DocumentId).IsDirty.Should().BeTrue();
        _ = service.CanUndo(original.DocumentId).Should().BeTrue();
        _ = service.CanUndo(copy.DocumentId).Should().BeFalse();
        var savingAgain = () => service.SaveCopyAsync(original.DocumentId, copyUri, this.TestContext.CancellationToken);
        _ = await savingAgain.Should().ThrowExactlyAsync<StorageWriteConflictException>().ConfigureAwait(false);
        var savingOriginal = () => service.SaveCopyAsync(original.DocumentId, original.MaterialUri, this.TestContext.CancellationToken);
        _ = await savingOriginal.Should().ThrowExactlyAsync<ArgumentException>().ConfigureAwait(false);
    }

    /// <summary>An explicitly approved reload adopts disk bytes and baseline and retires previous history.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ReloadAdoptsExternalBaselineAndRetiresHistory()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var original = await service.CreateAsync(new Uri("asset:///Content/Materials/Reload.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var externalWriter = CreateService(workspace);
        var external = await externalWriter.OpenAsync(original.MaterialUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await externalWriter.EditPropertiesAsync(external.DocumentId, PropertyEdit.Single(MaterialDescriptors.Roughness, 0.2f), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await externalWriter.SaveAsync(external.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await service.EditPropertiesAsync(original.DocumentId, PropertyEdit.Single(MaterialDescriptors.Roughness, 0.8f), this.TestContext.CancellationToken).ConfigureAwait(false);

        var reloaded = await service.ReloadAsync(original.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = reloaded.Source.PbrMetallicRoughness.RoughnessFactor.Should().Be(0.2f);
        _ = reloaded.MaterialGuid.Should().Be(original.MaterialGuid);
        _ = reloaded.IsDirty.Should().BeFalse();
        _ = service.CanUndo(original.DocumentId).Should().BeFalse();
        _ = service.CanRedo(original.DocumentId).Should().BeFalse();
        _ = (await service.SaveAsync(original.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
    }
}
