// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.World.Serialization;
using static Oxygen.Editor.ContentPipeline.TestSupport.IncrementalCookScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Cooking;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class TextureCookTests
{
    public TestContext TestContext { get; set; } = null!;
    /// <summary>Named textures remain source-associated and resolve from native material and scene imports.</summary>
    /// <returns>The asynchronous native workflow regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task NamedTextureCooksResolvesReusesAndRepairs()
    {
        using var workspace = new CookWorkspace();
        workspace.WriteText("Content/Textures/Meter.otex.json", """
            { "source": "Meter.tga", "intent": "data", "decode": { "color_space": "linear" },
              "output": { "format": "rgba8" } }
            """);
        byte[] image = [0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 32, 8, 255, 255, 255, 255];
        await File.WriteAllBytesAsync(Path.Combine(workspace.Root, "Content/Textures/Meter.tga"), image, this.TestContext.CancellationToken).ConfigureAwait(false);
        var texture = new Uri("asset:///Content/Textures/Meter.otex.json");
        workspace.Scene.SetEnvironment(new SceneEnvironmentData
        {
            PostProcess = new PostProcessEnvironmentData { AutoExposureMeteringMask = texture },
        });
        await workspace.WriteSceneAsync("Content/Scenes/Main.oscene.json").ConfigureAwait(false);
        workspace.WriteText("Content/Materials/Masked.omat.json", """
            { "name": "Masked", "textures": { "base_color": { "virtual_path": "/Content/Textures/Meter.otex" } } }
            """);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = CreateRecordingApi(compatibility);
        var service = CreateIncrementalService(workspace, api, compatibility);
        var first = await service.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(first);
        var output = first.CookedAssets.Should().ContainSingle(asset => asset.SourceAssetUri == texture).Subject;
        _ = output.Kind.Should().Be(ContentCookAssetKind.Texture);
        _ = output.VirtualPath.Should().Be("/Content/Textures/Meter.otex");
        _ = output.DescriptorRelativePath.Should().Be("Textures/Meter.otex");
        var root = workspace.CookedRoot("Content");
        var inventory = await api.ReadInventoryAsync(root, null, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = inventory.Resources.Should().ContainSingle(resource => resource.DescriptorPath == output.DescriptorRelativePath && resource.ResourceIndex != null);
        _ = inventory.Assets.Should().NotContain(static asset => asset.Type == 4);
        var status = (await service.ReadAsync(workspace.ProjectContext, [texture], this.TestContext.CancellationToken).ConfigureAwait(false)).Single();
        _ = status.Freshness.Should().Be(AssetCookFreshness.Current);
        _ = status.HasAvailableOutput.Should().BeTrue();
        api.Imported.Clear();
        _ = (await service.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
        _ = api.Imported.Should().BeEmpty();

        var beforeInvalidImport = ReadOutputIdentities(workspace.Root);
        workspace.WriteText("Invalid.otex.json", System.Text.Json.JsonSerializer.Serialize(new
        {
            source = Path.Combine(workspace.Root, "Content/Textures/Meter.tga"),
            virtual_path = "/Other/Meter.otex",
        }));
        var invalidManifest = new ContentImportManifest(1, root, new("/Content"),
            [new("invalid", "texture-descriptor", "Invalid.otex.json", [], Output: null, Name: "Invalid")]);
        var invalid = await api.ImportAsync(new(Guid.NewGuid(), workspace.Root, Path.Combine(workspace.Root, ".invalid-native"), invalidManifest), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = invalid.Succeeded.Should().BeFalse();
        _ = ReadOutputIdentities(workspace.Root).Should().BeEquivalentTo(beforeInvalidImport);

        var path = Path.Combine(root, output.DescriptorRelativePath!);
        var original = await File.ReadAllBytesAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false);
        var damaged = original.ToArray();
        damaged[^1] ^= 0xFF;
        await File.WriteAllBytesAsync(path, damaged, this.TestContext.CancellationToken).ConfigureAwait(false);
        var repaired = await service.CookAssetAsync(texture, this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertCookSucceeded(repaired);
        _ = repaired.CookedAssets.Should().ContainSingle(asset => asset.SourceAssetUri == texture);
        _ = (await File.ReadAllBytesAsync(Path.Combine(workspace.CookedRoot("Content"), output.DescriptorRelativePath!), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(original);
        _ = (await service.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
    }
}
