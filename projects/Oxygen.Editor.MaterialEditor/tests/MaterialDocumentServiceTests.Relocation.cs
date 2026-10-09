// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.MaterialEditor.PropertyPipeline;

namespace Oxygen.Editor.MaterialEditor.Tests;

/// <summary>Open material documents follow relocations in place, keeping their history.</summary>
public sealed partial class MaterialDocumentServiceTests
{
    /// <summary>A moved, saved material re-points its URI and file in place: still clean, history kept, cook input relocated.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task MovedMaterialFollowsInPlaceAndKeepsHistory()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var document = await service.CreateAsync(new Uri("asset:///Content/Materials/Red.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await service.EditScalarAsync(document.DocumentId, new MaterialFieldEdit(MaterialFieldKeys.MetallicFactor, 0.25f), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = (await service.SaveAsync(document.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        var target = Path.Combine(Path.GetDirectoryName(document.SourcePath)!, "Blue.omat.json");
        File.Move(document.SourcePath, target);
        var map = new RelocationMap();
        map.AddMove(new(document.SourcePath, target, IsDirectory: false));
        map.AddIdentity("/Content/Materials/Red.omat", "/Content/Materials/Blue.omat", isFolder: false);

        _ = service.FollowRelocation(document.DocumentId, new AssetRelocationChange(map, [], Task.FromResult<string?>(null))).Should().BeTrue();

        var followed = service.GetDocument(document.DocumentId);
        _ = followed.MaterialUri.Should().Be(new Uri("asset:///Content/Materials/Blue.omat.json"));
        _ = followed.SourcePath.Should().Be(target);
        _ = followed.IsDirty.Should().BeFalse();
        _ = service.CanUndo(document.DocumentId).Should().BeTrue("relocation does not touch the document's history");
        using var reads = await workspace.CookDocuments.AcquireAsync([target], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = reads.Documents.Should().ContainSingle().Which.IsDirty.Should().BeFalse();
    }

    /// <summary>A material whose file a relocation rewrote records the written version as saved and stays clean.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task RewrittenMaterialRecordsTheWrittenVersionAsSaved()
    {
        using var workspace = new TempWorkspace();
        var service = CreateService(workspace);
        var document = await service.CreateAsync(new Uri("asset:///Content/Materials/Red.omat.json"), this.TestContext.CancellationToken).ConfigureAwait(false);
        var map = new RelocationMap();
        map.AddIdentity("/Content/Textures/Wood.otex", "/Content/Textures/Oak.otex", isFolder: false);
        var written = new FileVersion(Exists: true, new string('C', 64));
        var unchanged = new AssetRelocationChange(map, [new RelocatedFile(document.SourcePath, document.SourcePath, written)], Task.FromResult<string?>(null));

        _ = service.FollowRelocation(document.DocumentId, unchanged).Should().BeTrue();

        using var reads = await workspace.CookDocuments.AcquireAsync([document.SourcePath], this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = reads.Documents.Should().ContainSingle().Which.SavedContentHash.Should().Be(written.Sha256);
        _ = service.GetDocument(document.DocumentId).IsDirty.Should().BeFalse();
    }
}
