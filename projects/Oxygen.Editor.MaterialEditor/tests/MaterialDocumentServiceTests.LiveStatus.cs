// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Schemas;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Material authoring publishes live dirty and saved-source state through its cook registration.</summary>
public sealed partial class MaterialDocumentServiceTests
{
    /// <summary>Preview, cancellation, history, save and close publish current state without rereading source.</summary>
    /// <returns>The asynchronous authoring-state regression.</returns>
    [TestMethod]
    public async Task MaterialStateTracksGesturesHistorySaveAndClose()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var changes = new List<CookDocumentStateChangedEventArgs>();
        workspace.CookDocuments.StateChanged += (_, change) => changes.Add(change);
        var document = await service.CreateAsync(new Uri("asset:///Content/Materials/LiveStatus.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.CookDocuments.GetState().Documents.Should().ContainSingle().Which.IsDirty.Should().BeFalse();
        var session = service.BeginEditSession(document.DocumentId, "Roughness");
        var edit = new PropertyEdit();
        edit.Set(MaterialDescriptors.Roughness, 0.25f);
        _ = (await service.PreviewPropertiesAsync(session, edit, this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = workspace.CookDocuments.GetState().Documents.Should().ContainSingle().Which.IsDirty.Should().BeTrue();
        var dirtyVersion = workspace.CookDocuments.GetState().Version;
        edit.Set(MaterialDescriptors.Roughness, 0.75f);
        _ = await service.PreviewPropertiesAsync(session, edit, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.CookDocuments.GetState().Version.Should().Be(dirtyVersion);
        _ = service.CompleteEditSession(session, commit: false);
        _ = workspace.CookDocuments.GetState().Documents.Should().ContainSingle().Which.IsDirty.Should().BeFalse();
        _ = await service.EditPropertiesAsync(document.DocumentId, edit, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.CookDocuments.GetState().Documents.Should().ContainSingle().Which.IsDirty.Should().BeTrue();
        _ = service.Undo(document.DocumentId);
        _ = workspace.CookDocuments.GetState().Documents.Should().ContainSingle().Which.IsDirty.Should().BeFalse();
        _ = service.Redo(document.DocumentId);
        _ = workspace.CookDocuments.GetState().Documents.Should().ContainSingle().Which.IsDirty.Should().BeTrue();
        _ = (await service.SaveAsync(document.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = workspace.CookDocuments.GetState().Documents.Should().ContainSingle().Which.IsDirty.Should().BeFalse();
        _ = changes.Should().Contain(change => change.SavedSourceChanged);
        await service.CloseAsync(document.DocumentId, discard: false, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.CookDocuments.GetState().Documents.Should().BeEmpty();
    }
}
