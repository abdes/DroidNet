// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Checks saving a copy of a material at the document owner.</summary>
public sealed partial class MaterialDocumentServiceTests
{
    /// <summary>A saved copy writes the edited material to the new source and leaves the original document unsaved.</summary>
    /// <returns>The asynchronous copy regression.</returns>
    [TestMethod]
    public async Task MaterialSaveCopyWritesOnlyTheNewSource()
    {
        using var workspace = new TempWorkspace();
        var service = new MaterialDocumentService(new TestResolver(workspace.Root), new RecordingCookService(), workspace.CookDocuments, CreateFileStore());
        var original = await service.CreateAsync(new("asset:///Content/Materials/Original.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var originalBytes = await File.ReadAllBytesAsync(original.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await service.EditScalarAsync(original.DocumentId, new(MaterialFieldKeys.RoughnessFactor, 0.8f), this.TestContext.CancellationToken).ConfigureAwait(false);

        var copyUri = await service.SaveCopyAsync(original.DocumentId, new("asset:///Content/Materials/Copy.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);

        var copy = await service.OpenAsync(copyUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = File.Exists(copy.SourcePath).Should().BeTrue();
        _ = service.GetDocument(original.DocumentId).IsDirty.Should().BeTrue();
        _ = service.CanUndo(original.DocumentId).Should().BeTrue();
        _ = (await File.ReadAllBytesAsync(original.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(originalBytes);
    }

    /// <summary>A copy onto an existing source is refused and leaves that source as it was.</summary>
    /// <returns>The asynchronous rejected-copy regression.</returns>
    [TestMethod]
    public async Task ConflictingMaterialCopyIsRefused()
    {
        using var workspace = new TempWorkspace();
        var service = new MaterialDocumentService(new TestResolver(workspace.Root), new RecordingCookService(), workspace.CookDocuments, CreateFileStore());
        var original = await service.CreateAsync(new("asset:///Content/Materials/Original.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var copyUri = new Uri("asset:///Content/Materials/Existing.omat.json");
        var existing = await service.CreateAsync(copyUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        var existingBytes = await File.ReadAllBytesAsync(existing.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false);

        Func<Task> save = async () => _ = await service.SaveCopyAsync(original.DocumentId, copyUri, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = await save.Should().ThrowAsync<DroidNet.Storage.StorageWriteConflictException>().ConfigureAwait(false);
        _ = (await File.ReadAllBytesAsync(existing.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(existingBytes);
    }
}
