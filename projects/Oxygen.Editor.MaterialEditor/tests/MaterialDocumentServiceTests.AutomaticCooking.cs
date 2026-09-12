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
