// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.MaterialEditor.PropertyPipeline;
using Oxygen.Editor.Schemas;

namespace Oxygen.Editor.MaterialEditor.Tests;

public sealed partial class MaterialDocumentServiceTests
{
    /// <summary>Assigning, replacing, clearing, undoing and saving textures preserves other channel data.</summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task EditTextureAsyncAssignsReplacesClearsAndPersistsChannelBindings()
    {
        using var workspace = new TempWorkspace();
        const string relativePath = "Content/Materials/Maps.omat.json";
        var sourcePath = Path.Combine(workspace.Root, relativePath);
        Directory.CreateDirectory(Path.GetDirectoryName(sourcePath)!);
        await File.WriteAllTextAsync(sourcePath, """
            {
              "name": "Maps",
              "textures": {
                "base_color": { "virtual_path": "/Content/Textures/Old.otex", "uv_set": 2,
                  "uv_transform": { "scale": [2, 3], "offset": [0.1, 0.2], "rotation_radians": 0.5 } },
                "normal": { "virtual_path": "/Content/Textures/Normal.otex", "uv_set": 1 },
                "emissive": { "virtual_path": "/Content/Textures/Glow.otex" }
              },
              "parameters": { "normal_scale": 0.75, "ambient_occlusion": 0.4 }
            }
            """, this.TestContext.CancellationToken).ConfigureAwait(false);
        var service = CreateService(workspace);
        var material = await service.OpenAsync(
            new Uri("asset:///Content/Materials/Maps.omat.json"),
            this.TestContext.CancellationToken).ConfigureAwait(false);

        var assigned = await service.EditTextureAsync(material.DocumentId, "base_color", "/Content/Textures/New.otex", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = assigned.Succeeded.Should().BeTrue();
        _ = service.GetDocument(material.DocumentId).IsDirty.Should().BeTrue();
        _ = service.CanUndo(material.DocumentId).Should().BeTrue();
        var replacedJson = Oxygen.Managed.Assets.Authoring.Materials.MaterialSourceWriter.ToJson(service.GetDocument(material.DocumentId).Source);
        _ = replacedJson["textures"]!["base_color"]!["virtual_path"]!.GetValue<string>().Should().Be("/Content/Textures/New.otex");
        _ = replacedJson["textures"]!["base_color"]!["uv_set"]!.GetValue<int>().Should().Be(2);
        _ = replacedJson["textures"]!["base_color"]!["uv_transform"]!["rotation_radians"]!.GetValue<float>().Should().Be(0.5f);

        _ = service.Undo(material.DocumentId).Succeeded.Should().BeTrue();
        _ = service.GetDocument(material.DocumentId).Source.TextureReferences["base_color"].Should().Be("/Content/Textures/Old.otex");
        _ = service.Redo(material.DocumentId).Succeeded.Should().BeTrue();
        _ = service.GetDocument(material.DocumentId).Source.TextureReferences["base_color"].Should().Be("/Content/Textures/New.otex");

        var cleared = await service.EditTextureAsync(material.DocumentId, "normal", virtualPath: null, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = cleared.Succeeded.Should().BeTrue();
        _ = (await service.SaveAsync(material.DocumentId, this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        await service.CloseAsync(material.DocumentId, discard: false, this.TestContext.CancellationToken).ConfigureAwait(false);
        var reopened = await service.OpenAsync(material.MaterialUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        var saved = Oxygen.Managed.Assets.Authoring.Materials.MaterialSourceWriter.ToJson(reopened.Source);

        _ = saved["textures"]!.AsObject().ContainsKey("normal").Should().BeFalse();
        _ = saved["textures"]!["base_color"]!["virtual_path"]!.GetValue<string>().Should().Be("/Content/Textures/New.otex");
        _ = saved["textures"]!["base_color"]!["uv_set"]!.GetValue<int>().Should().Be(2);
        _ = saved["textures"]!["emissive"]!["virtual_path"]!.GetValue<string>().Should().Be("/Content/Textures/Glow.otex");
        _ = saved["parameters"]!["normal_scale"]!.GetValue<float>().Should().Be(0.75f);
        _ = JsonNode.DeepEquals(saved["textures"]!["base_color"]!["uv_transform"], replacedJson["textures"]!["base_color"]!["uv_transform"]).Should().BeTrue();
    }

    /// <summary>Schema-backed scalar edits observe texture settings even when their maps are unassigned.</summary>
    /// <returns>The asynchronous test task.</returns>
    [TestMethod]
    public async Task TextureSettingsWithoutAssignedMapsRemainNoOpWhenUnchanged()
    {
        using var workspace = new TempWorkspace();
        var sourcePath = Path.Combine(workspace.Root, "Content", "Materials", "Settings.omat.json");
        Directory.CreateDirectory(Path.GetDirectoryName(sourcePath)!);
        await File.WriteAllTextAsync(sourcePath, """
            { "name": "Settings", "parameters": { "normal_scale": 0.75, "ambient_occlusion": 0.4 } }
            """, this.TestContext.CancellationToken).ConfigureAwait(false);
        var service = CreateService(workspace);
        var material = await service.OpenAsync(
            new Uri("asset:///Content/Materials/Settings.omat.json"),
            this.TestContext.CancellationToken).ConfigureAwait(false);

        var normal = await service.EditPropertiesAsync(
            material.DocumentId,
            PropertyEdit.SingleEdit(MaterialDescriptors.NormalScale, 0.75f),
            this.TestContext.CancellationToken).ConfigureAwait(false);
        var occlusion = await service.EditPropertiesAsync(
            material.DocumentId,
            PropertyEdit.SingleEdit(MaterialDescriptors.AmbientOcclusion, 0.4f),
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = normal.Succeeded.Should().BeTrue();
        _ = occlusion.Succeeded.Should().BeTrue();
        _ = service.GetDocument(material.DocumentId).IsDirty.Should().BeFalse();
    }
}
