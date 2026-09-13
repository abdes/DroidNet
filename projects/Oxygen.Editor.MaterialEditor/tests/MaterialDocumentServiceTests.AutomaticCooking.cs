// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentPipeline.Cooking;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Checks the Save trigger at the document owner, including creation, unchanged saves, and conflicts.</summary>
public sealed partial class MaterialDocumentServiceTests
{
    /// <summary>A saved copy schedules its own acknowledged bytes while preserving the original document.</summary>
    /// <returns>The asynchronous copy-trigger regression.</returns>
    [TestMethod]
    public async Task MaterialSaveCopySchedulesOnlyTheNewSource()
    {
        using var workspace = new TempWorkspace();
        var automatic = new Mock<IAutomaticCookService>();
        var service = new MaterialDocumentService(automatic.Object, new TestResolver(workspace.Root), new RecordingCookService(), workspace.CookDocuments, CreateFileStore());
        var original = await service.CreateAsync(new("asset:///Content/Materials/Original.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var originalBytes = await File.ReadAllBytesAsync(original.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await service.EditScalarAsync(original.DocumentId, new(MaterialFieldKeys.RoughnessFactor, 0.8f), this.TestContext.CancellationToken).ConfigureAwait(false);
        automatic.Invocations.Clear();
        var copyUri = await service.SaveCopyAsync(original.DocumentId, new("asset:///Content/Materials/Copy.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var copy = await service.OpenAsync(copyUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        var hash = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(await File.ReadAllBytesAsync(copy.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false)));
        automatic.Verify(value => value.NotifySaved(copy.SourcePath, It.Is<string>(value => string.Equals(value, hash, StringComparison.OrdinalIgnoreCase)), contentChanged: true), Times.Once);
        automatic.VerifyNoOtherCalls();
        _ = service.GetDocument(original.DocumentId).IsDirty.Should().BeTrue();
        _ = service.CanUndo(original.DocumentId).Should().BeTrue();
        _ = (await File.ReadAllBytesAsync(original.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(originalBytes);
    }

    /// <summary>A conflicting copy destination does not schedule cooking.</summary>
    /// <returns>The asynchronous rejected-copy regression.</returns>
    [TestMethod]
    public async Task ConflictingMaterialCopyDoesNotScheduleCooking()
    {
        using var workspace = new TempWorkspace();
        var automatic = new Mock<IAutomaticCookService>();
        var service = new MaterialDocumentService(automatic.Object, new TestResolver(workspace.Root), new RecordingCookService(), workspace.CookDocuments, CreateFileStore());
        var original = await service.CreateAsync(new("asset:///Content/Materials/Original.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var copyUri = new Uri("asset:///Content/Materials/Existing.omat.json");
        _ = await service.CreateAsync(copyUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        automatic.Invocations.Clear();
        Func<Task> save = async () => _ = await service.SaveCopyAsync(original.DocumentId, copyUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await save.Should().ThrowAsync<DroidNet.Storage.StorageWriteConflictException>().ConfigureAwait(false);
        automatic.VerifyNoOtherCalls();
    }

    /// <summary>Only acknowledged saved material bytes trigger cooking; opening and transient edits do not.</summary>
    /// <returns>The asynchronous document regression.</returns>
    [TestMethod]
    public async Task MaterialSaveNotifiesAutomaticCookingWithAcknowledgedBytes()
    {
        using var workspace = new TempWorkspace();
        var automatic = new Mock<IAutomaticCookService>();
        var service = new MaterialDocumentService(automatic.Object, new TestResolver(workspace.Root), new RecordingCookService(), workspace.CookDocuments, CreateFileStore());
        var uri = new Uri("asset:///Content/Materials/Automatic.omat.json");
        var document = await service.CreateAsync(uri, this.TestContext.CancellationToken).ConfigureAwait(false);
        automatic.Verify(value => value.NotifySaved(document.SourcePath, It.IsAny<string>(), contentChanged: true), Times.Once);
        automatic.Invocations.Clear();
        _ = await service.EditScalarAsync(document.DocumentId, new(MaterialFieldKeys.RoughnessFactor, 0.8f), this.TestContext.CancellationToken).ConfigureAwait(false);
        automatic.VerifyNoOtherCalls();
        _ = (await service.SaveAsync(document.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        var hash = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(await File.ReadAllBytesAsync(document.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false)));
        automatic.Verify(value => value.NotifySaved(document.SourcePath, It.Is<string>(value => string.Equals(value, hash, StringComparison.OrdinalIgnoreCase)), contentChanged: true), Times.Once);
        automatic.Invocations.Clear();
        _ = await service.SaveAsync(document.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false);
        automatic.Verify(value => value.NotifySaved(document.SourcePath, It.IsAny<string>(), contentChanged: false), Times.Once);
        automatic.Invocations.Clear();
        await File.WriteAllTextAsync(document.SourcePath, "external bytes", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = (await service.SaveAsync(document.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false)).IsConflict.Should().BeTrue();
        automatic.VerifyNoOtherCalls();
    }
}
