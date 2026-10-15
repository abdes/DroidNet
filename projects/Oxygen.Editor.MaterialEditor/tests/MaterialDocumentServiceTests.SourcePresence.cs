// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.MaterialEditor.PropertyPipeline;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Verifies that an open material survives its file being deleted or moved outside the editor.</summary>
public sealed partial class MaterialDocumentServiceTests
{
    /// <summary>A deleted file is reported missing, and saving the open material writes it again.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task DeletedSourceIsMissingUntilSavingWritesItAgain()
    {
        using var workspace = new TempWorkspace();
        var service = new MaterialDocumentService(Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(), new TestResolver(workspace.Root), new RecordingCookService(), workspace.CookDocuments, CreateFileStore());
        var document = await service.CreateAsync(new Uri("asset:///Content/Materials/Gone.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        File.Delete(document.SourcePath);

        _ = (await service.RefreshSourcePresenceAsync(document.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        var saved = await service.SaveAsync(document.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = saved.Succeeded.Should().BeTrue();
        _ = File.Exists(document.SourcePath).Should().BeTrue();
        _ = (await service.RefreshSourcePresenceAsync(document.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeFalse();
    }

    /// <summary>A file restored unchanged is in step with the document again, so later saves succeed normally.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task SourceRestoredUnchangedIsInStepAgain()
    {
        using var workspace = new TempWorkspace();
        var service = new MaterialDocumentService(Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(), new TestResolver(workspace.Root), new RecordingCookService(), workspace.CookDocuments, CreateFileStore());
        var document = await service.CreateAsync(new Uri("asset:///Content/Materials/Back.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var original = await File.ReadAllBytesAsync(document.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false);
        File.Delete(document.SourcePath);
        _ = await service.RefreshSourcePresenceAsync(document.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false);
        await File.WriteAllBytesAsync(document.SourcePath, original, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = (await service.RefreshSourcePresenceAsync(document.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeFalse();
        _ = await service.EditScalarAsync(document.DocumentId, new(MaterialFieldKeys.RoughnessFactor, 0.8f), this.TestContext.CancellationToken).ConfigureAwait(false);
        var saved = await service.SaveAsync(document.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = saved.Succeeded.Should().BeTrue();
        _ = saved.IsConflict.Should().BeFalse();
    }

    /// <summary>A file restored with other content is an external change, so saving reports the ordinary conflict.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task SourceRestoredWithOtherContentConflictsOnSave()
    {
        using var workspace = new TempWorkspace();
        var service = new MaterialDocumentService(Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(), new TestResolver(workspace.Root), new RecordingCookService(), workspace.CookDocuments, CreateFileStore());
        var document = await service.CreateAsync(new Uri("asset:///Content/Materials/Other.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var external = await File.ReadAllTextAsync(document.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false) + "\n ";
        File.Delete(document.SourcePath);
        _ = await service.RefreshSourcePresenceAsync(document.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false);
        await File.WriteAllTextAsync(document.SourcePath, external, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = (await service.RefreshSourcePresenceAsync(document.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeFalse();
        var saved = await service.SaveAsync(document.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = saved.IsConflict.Should().BeTrue();
        _ = (await File.ReadAllTextAsync(document.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(external);
    }
}
