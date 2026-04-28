// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Tests;

[TestClass]
public sealed class ContentPipelineContractTests
{
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
                    Name: "Red"),
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
    public void ToNativeDescriptorPath_ShouldStripEditorJsonSuffix()
    {
        _ = ContentPipelinePaths.ToNativeDescriptorPath(
                new Uri("asset:///Content/Materials/Red.omat.json"),
                ".omat")
            .Should().Be("/Content/Materials/Red.omat");
        _ = ContentPipelinePaths.ToNativeDescriptorPath(
                new Uri("asset:///Content/Geometry/Cube.ogeo.json"),
                ".ogeo")
            .Should().Be("/Content/Geometry/Cube.ogeo");
        _ = ContentPipelinePaths.ToNativeDescriptorPath(
                new Uri("asset:///Content/Scenes/Main.oscene.json"),
                ".oscene")
            .Should().Be("/Content/Scenes/Main.oscene");
    }

    [TestMethod]
    public void ToNativeDescriptorPath_WhenExtensionDoesNotMatch_ShouldReject()
    {
        var act = () => ContentPipelinePaths.ToNativeDescriptorPath(
            new Uri("asset:///Content/Materials/Red.omat.json"),
            ".ogeo");

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void NormalizeSceneDescriptorName_ShouldProduceNativeIdentifier()
    {
        _ = ContentPipelinePaths.NormalizeSceneDescriptorName("Main Scene 01.oscene.json")
            .Should().Be("Main_Scene_01");
        _ = ContentPipelinePaths.NormalizeSceneDescriptorName("-starter scene.oscene.json")
            .Should().Be("_-starter_scene");
        _ = ContentPipelinePaths.NormalizeSceneDescriptorName(new string('A', 80) + ".oscene.json")
            .Should().HaveLength(63);
    }

    [TestMethod]
    public void MountLayout_ShouldUsePerMountCookedRootAndVirtualRoot()
    {
        _ = ContentPipelinePaths.GetCookedMountRoot(@"C:\Project", "Content")
            .Should().Be(Path.Combine(@"C:\Project", ".cooked", "Content"));
        _ = ContentPipelinePaths.GetVirtualMountRoot("Content").Should().Be("/Content");
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
                    Name: "Main"),
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
