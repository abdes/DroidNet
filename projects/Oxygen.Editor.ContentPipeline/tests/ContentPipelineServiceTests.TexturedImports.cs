// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Qualifies static textured models through source retention, publication and reimport.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>External images remain portable after the original files disappear and the retained model is edited.</summary>
    /// <param name="extension">The native model format.</param>
    /// <returns>The import/reimport verification.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow("gltf")]
    [DataRow("fbx")]
    public async Task TexturedModelImportRetainsItsImageAndReimports(string extension)
    {
        using var workspace = new TempWorkspace();
        var original = Path.Combine(workspace.Root, "incoming");
        _ = Directory.CreateDirectory(original);
        var primary = Path.Combine(original, "model." + extension);
        File.Copy(Path.Combine(AppContext.BaseDirectory, "Fixtures", "static_textured_triangle." + extension), primary);
        const string imageName = "static_textured_checker.png";
        File.Copy(Path.Combine(AppContext.BaseDirectory, "Fixtures", imageName), Path.Combine(original, imageName));
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var pipeline = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var request = new SceneImportRequest(workspace.ProjectContext, primary, "Textured", new Uri("asset:///Content/Models"));

        var imported = await pipeline.ImportSourceAsync(request, this.TestContext.CancellationToken).ConfigureAwait(false);

        AssertCookSucceeded(imported);
        _ = imported.IsPublished.Should().BeTrue();
        _ = imported.Diagnostics.Should().NotContain(issue => issue.Code == "material.texture_missing");
        var retainedUri = imported.RetainedSourceUri!;
        var retained = CookInputResolver.Resolve(workspace.ProjectContext, retainedUri, ContentCookInputRole.Primary).SourceAbsolutePath;
        var retainedImage = Path.Combine(Path.GetDirectoryName(retained)!, imageName);
        _ = File.Exists(retainedImage).Should().BeTrue();
        _ = (await File.ReadAllBytesAsync(retainedImage, this.TestContext.CancellationToken).ConfigureAwait(false))
            .Should().Equal(await File.ReadAllBytesAsync(Path.Combine(original, imageName), this.TestContext.CancellationToken).ConfigureAwait(false));
        var settings = NativeSceneImportSettings.Parse(await File.ReadAllBytesAsync(retained + NativeSceneImportSettings.SidecarSuffix, this.TestContext.CancellationToken).ConfigureAwait(false));
        _ = settings.ContentPolicy.Should().Be("static");
        _ = settings.TangentsPolicy.Should().Be("generate");
        _ = settings.MaterialSlotProvenance.SourceIdentity.Should().Be(request.Provenance.SourceIdentity);

        File.Delete(primary);
        File.Delete(Path.Combine(original, imageName));
        var text = await File.ReadAllTextAsync(retained, this.TestContext.CancellationToken).ConfigureAwait(false);
        if (string.Equals(extension, "gltf", StringComparison.Ordinal))
        {
            var document = JsonNode.Parse(text)!;
            document["nodes"]![0]!["translation"]![0] = 9;
            text = document.ToJsonString();
        }
        else
        {
            text = text.Replace("\"A\",1,2,3", "\"A\",9,2,3", StringComparison.Ordinal);
        }

        await File.WriteAllTextAsync(retained, text, this.TestContext.CancellationToken).ConfigureAwait(false);
        var reimported = await pipeline.ReimportSourceAsync(retainedUri, workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(reimported);
        _ = reimported.IsPublished.Should().BeTrue();
        _ = reimported.Diagnostics.Should().NotContain(issue => issue.Code == "material.texture_missing");
        _ = (await pipeline.CookAssetAsync(retainedUri, this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
    }
}
