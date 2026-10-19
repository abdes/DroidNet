// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

[TestClass]
public sealed class ContentPipelineContractTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public void ContentPipelineOperationKinds_ShouldMatchAcceptedVocabulary()
    {
        _ = ContentPipelineOperationKinds.DescriptorGenerate.Should().Be("Content.Descriptor.Generate");
        _ = ContentPipelineOperationKinds.ManifestGenerate.Should().Be("Content.Manifest.Generate");
        _ = ContentPipelineOperationKinds.Import.Should().Be("Content.Import");
        _ = ContentPipelineOperationKinds.CookAsset.Should().Be("Content.Cook.Asset");
        _ = ContentPipelineOperationKinds.CookScene.Should().Be("Content.Cook.Scene");
        _ = ContentPipelineOperationKinds.CookFolder.Should().Be("Content.Cook.Folder");
        _ = ContentPipelineOperationKinds.CookProject.Should().Be("Content.Cook.Project");
        _ = ContentPipelineOperationKinds.CookedOutputInspect.Should().Be("Content.CookedOutput.Inspect");
        _ = ContentPipelineOperationKinds.CookedOutputValidate.Should().Be("Content.CookedOutput.Validate");
        _ = ContentPipelineOperationKinds.CatalogRefresh.Should().Be("Content.Catalog.Refresh");
        _ = RuntimeOperationKinds.CookedRootRefresh.Should().Be("Runtime.CookedRoot.Refresh");
    }

    [TestMethod]
    public void DiagnosticCodes_ShouldMatchAcceptedPrefixes()
    {
        _ = ContentPipelineDiagnosticCodes.SceneDescriptorGenerationFailed
            .Should().Be("OXE.CONTENTPIPELINE.SCENE.DescriptorGenerationFailed");
        _ = ContentPipelineDiagnosticCodes.SceneUnsupportedField
            .Should().Be("OXE.CONTENTPIPELINE.SCENE.UnsupportedField");
        _ = ContentPipelineDiagnosticCodes.GeometryDescriptorGenerationFailed
            .Should().Be("OXE.CONTENTPIPELINE.GEOMETRY.DescriptorGenerationFailed");
        _ = ContentPipelineDiagnosticCodes.ManifestGenerationFailed
            .Should().Be("OXE.CONTENTPIPELINE.MANIFEST.GenerationFailed");
        _ = ContentPipelineDiagnosticCodes.InspectFailed
            .Should().Be("OXE.CONTENTPIPELINE.INSPECT.Failed");
        _ = ContentPipelineDiagnosticCodes.ValidateFailed
            .Should().Be("OXE.CONTENTPIPELINE.VALIDATE.Failed");
        _ = AssetImportDiagnosticCodes.SourceMissing.Should().Be("OXE.ASSETIMPORT.SourceMissing");
        _ = AssetImportDiagnosticCodes.ImportFailed.Should().Be("OXE.ASSETIMPORT.ImportFailed");
        _ = AssetCookDiagnosticCodes.CookFailed.Should().Be("OXE.ASSETCOOK.CookFailed");
        _ = AssetCookDiagnosticCodes.IndexInvalid.Should().Be("OXE.ASSETCOOK.IndexInvalid");
        _ = AssetIdentityDiagnosticCodes.RefreshFailed.Should().Be("OXE.ASSETID.RefreshFailed");
        _ = AssetMountDiagnosticCodes.RefreshFailed.Should().Be("OXE.ASSETMOUNT.RefreshFailed");
    }

    [TestMethod]
    public void ContentImportManifest_ShouldSerializeWithNativeSchemaPropertyNames()
    {
        var manifest = new ContentImportManifest(
            Version: 1,
            Output: @"C:\Project\.cooked\Content",
            Layout: new ContentImportLayout("/Content"),
            Jobs:
            [
                new ContentImportJob(
                    Id: "material-red",
                    Type: "material-descriptor",
                    Source: "Content/Materials/Red.omat.json",
                    DependsOn: [],
                    Output: null,
                    Name: "Red")
                {
                    VirtualPath = "/Content/Materials/Red.omat",
                },
            ]);

        var json = JsonSerializer.Serialize(manifest);

        _ = json.Should().Contain("\"version\":1");
        _ = json.Should().Contain("\"output\":");
        _ = json.Should().Contain("\"layout\":");
        _ = json.Should().Contain("\"virtual_mount_root\":\"/Content\"");
        _ = json.Should().Contain("\"jobs\":");
        _ = json.Should().Contain("\"depends_on\":[]");
        _ = json.Should().NotContain("\"output\":null");
        _ = json.Should().NotContain("VirtualMountRoot");
        _ = json.Should().NotContain("DependsOn");
    }

    [TestMethod]
    public void ToNativeDescriptorPath_ShouldReturnTheIdentityPath()
    {
        _ = ContentPipelinePaths.ToNativeDescriptorPath(
                AssetPath.Parse("/Content/Materials/Red.omat"),
                ".omat")
            .Should().Be("/Content/Materials/Red.omat");
        _ = ContentPipelinePaths.ToNativeDescriptorPath(
                AssetPath.Parse("/Content/Geometry/Cube.ogeo"),
                ".ogeo")
            .Should().Be("/Content/Geometry/Cube.ogeo");
        _ = ContentPipelinePaths.ToNativeDescriptorPath(
                AssetPath.Parse("/Content/Scenes/Main.oscene"),
                ".oscene")
            .Should().Be("/Content/Scenes/Main.oscene");
    }

    [TestMethod]
    public void ToNativeDescriptorPath_WhenExtensionDoesNotMatch_ShouldReject()
    {
        var act = () => ContentPipelinePaths.ToNativeDescriptorPath(
            AssetPath.Parse("/Content/Materials/Red.omat.json"),
            ".ogeo");

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void ToNativeDescriptorPath_WhenGivenTheAuthoringSuffix_ShouldReject()
    {
        var act = () => ContentPipelinePaths.ToNativeDescriptorPath(
            AssetPath.Parse("/Content/Materials/Red.omat.json"),
            ".omat");

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void ToCookedScenePath_ShouldKeepTheSceneName()
    {
        var longName = new string('A', 80);
        _ = ContentPipelinePaths.ToCookedScenePath(AssetPath.Parse("/Content/Scenes/Main Scene 01.oscene"))
            .Should().Be("/Content/Scenes/Main Scene 01.oscene");
        _ = ContentPipelinePaths.ToCookedScenePath(AssetPath.Parse("/Content/Scenes/-starter.v2 (1).oscene"))
            .Should().Be("/Content/Scenes/-starter.v2 (1).oscene");
        _ = ContentPipelinePaths.ToCookedScenePath(AssetPath.Parse($"/Content/Scenes/{longName}.oscene"))
            .Should().Be($"/Content/Scenes/{longName}.oscene");
    }

    [TestMethod]
    public void ContentImportManifestValidator_WhenManifestViolatesSchemaSlice_ShouldReturnDiagnostics()
    {
        var validator = new ContentImportManifestValidator();
        var manifest = new ContentImportManifest(
            Version: 2,
            Output: string.Empty,
            Layout: new ContentImportLayout("Content"),
            Jobs:
            [
                new ContentImportJob(
                    Id: "scene-main",
                    Type: "scene-descriptor",
                    Source: "Content/Scenes/Main.oscene.json",
                    DependsOn: ["missing-dependency"],
                    Output: null,
                    Name: "Main")
                {
                    VirtualPath = "/Content/Scenes/Main.oscene",
                },
            ]);

        var diagnostics = validator.Validate(Guid.NewGuid(), manifest);

        _ = diagnostics.Should().Contain(diagnostic =>
            diagnostic.Code == ContentPipelineDiagnosticCodes.ManifestGenerationFailed
            && diagnostic.Message.Contains("version", StringComparison.OrdinalIgnoreCase));
        _ = diagnostics.Should().Contain(diagnostic =>
            diagnostic.Message.Contains("output root", StringComparison.OrdinalIgnoreCase));
        _ = diagnostics.Should().Contain(diagnostic =>
            diagnostic.Message.Contains("virtual mount root", StringComparison.OrdinalIgnoreCase));
        _ = diagnostics.Should().Contain(diagnostic =>
            diagnostic.Message.Contains("unknown dependency", StringComparison.OrdinalIgnoreCase));
    }
}
