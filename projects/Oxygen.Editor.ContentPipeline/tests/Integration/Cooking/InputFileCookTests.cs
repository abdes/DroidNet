// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Cooking;

/// <summary>
/// Input sources cook through the native import tool, each product named under its source.
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class InputFileCookTests
{
    private const string Player = """
        {
          "actions": [ { "name": "Jump", "type": "bool" } ],
          "contexts": [ { "name": "Gameplay", "mappings": [ { "action": "Jump", "slot": "Space", "trigger": "pressed" } ] } ]
        }
        """;

    private const string UsesCommonFire = """
        {
          "contexts": [ { "name": "Gameplay", "mappings": [ { "action": "/Content/Common/Common.input/Fire.oiact", "slot": "F" } ] } ]
        }
        """;

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Cooking the folder of an input source publishes its action and context under it, then reuses them.</summary>
    /// <returns>The asynchronous cook.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task CookFolderPublishesInputActionsAndContextsUnderTheirSource()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteText("Content/Gameplay/Player.input.json", Player);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var service = CreateInputService(workspace, compatibility);

        var result = await service.CookFolderAsync(AssetPath.Parse("/Content/Gameplay"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.IsPublished.Should().BeTrue(Describe(result));
        _ = File.Exists(workspace.CookedPath("Content/Gameplay/Player.input/Jump.oiact")).Should().BeTrue();
        _ = File.Exists(workspace.CookedPath("Content/Gameplay/Player.input/Gameplay.oimap")).Should().BeTrue();
        var again = await service.CookFolderAsync(AssetPath.Parse("/Content/Gameplay"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = again.IsUpToDate.Should().BeTrue(
            $"status {again.Status}; cooked [{string.Join(", ", again.CookedAssets.Select(static asset => asset.CookedAssetUri))}]; "
            + string.Join("; ", again.Diagnostics.Select(static issue => issue.Code + ": " + (issue.TechnicalMessage ?? issue.Message))));
    }

    /// <summary>An input source cooks by its identity, as cook on change names a source that changed.</summary>
    /// <returns>The asynchronous cook.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task AnInputSourceCooksByItsIdentity()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteText("Content/Gameplay/Player.input.json", Player);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var service = CreateInputService(workspace, compatibility);

        var result = await service.CookAssetAsync(AssetPath.Parse("/Content/Gameplay/Player.input"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.IsPublished.Should().BeTrue(Describe(result));
        _ = File.Exists(workspace.CookedPath("Content/Gameplay/Player.input/Jump.oiact")).Should().BeTrue();
    }

    /// <summary>Two input sources in one folder that declare the same names cook four products, each attributed to its
    /// source.</summary>
    /// <returns>The asynchronous cook.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task SourcesInOneFolderDeclaringOneNameKeepTheirProductsApart()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteText("Content/Gameplay/Player.input.json", Player);
        workspace.WriteText("Content/Gameplay/Vehicle.input.json", Player);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var service = CreateInputService(workspace, compatibility);

        var result = await service.CookFolderAsync(AssetPath.Parse("/Content/Gameplay"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.IsPublished.Should().BeTrue(Describe(result));
        _ = result.CookedAssets.ToDictionary(static asset => asset.CookedAssetUri.VirtualPath, static asset => asset.SourceAssetUri.VirtualPath).Should().BeEquivalentTo(new Dictionary<string, string>
        {
            ["/Content/Gameplay/Player.input/Jump.oiact"] = "/Content/Gameplay/Player.input",
            ["/Content/Gameplay/Player.input/Gameplay.oimap"] = "/Content/Gameplay/Player.input",
            ["/Content/Gameplay/Vehicle.input/Jump.oiact"] = "/Content/Gameplay/Vehicle.input",
            ["/Content/Gameplay/Vehicle.input/Gameplay.oimap"] = "/Content/Gameplay/Vehicle.input",
        });
    }

    /// <summary>A mapping names an action of another source by its path; the other source cooks first.</summary>
    /// <returns>The asynchronous cook.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task AMappingUsesAnotherSourcesActionByPath()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteText("Content/Common/Common.input.json", """{ "actions": [ { "name": "Fire", "type": "bool" } ] }""");
        workspace.WriteText("Content/Gameplay/Player.input.json", UsesCommonFire);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var service = CreateInputService(workspace, compatibility);

        var result = await service.CookFolderAsync(AssetPath.Parse("/Content/Gameplay"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.IsPublished.Should().BeTrue(Describe(result));
        _ = File.Exists(workspace.CookedPath("Content/Common/Common.input/Fire.oiact")).Should().BeTrue("the referenced source cooks with the one that uses it");
        _ = File.Exists(workspace.CookedPath("Content/Gameplay/Player.input/Gameplay.oimap")).Should().BeTrue();
    }

    /// <summary>A mapping that names an action nothing declares fails the cook: no project source and no library
    /// produces it.</summary>
    /// <returns>The asynchronous cook.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task AMappingToAMissingActionFails()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteText("Content/Gameplay/Player.input.json", UsesCommonFire);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var service = CreateInputService(workspace, compatibility);

        var result = await service.CookFolderAsync(AssetPath.Parse("/Content/Gameplay"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.IsPublished.Should().BeFalse();
        _ = result.Diagnostics.Should().Contain(static diagnostic => diagnostic.Code == "asset_cook.library_reference_missing");
    }

    private static string Describe(ContentCookResult result)
        => string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message));

    private static ContentPipelineService CreateInputService(CookWorkspace workspace, EditorNativeCompatibilityService compatibility)
    {
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        return CreateService(workspace, api, compatibility: compatibility);
    }
}
