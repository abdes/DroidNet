// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Cooking;

/// <summary>
/// Hand-written input files cook through the native import tool into their own folder.
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

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Cooking the folder of an input file publishes its action and context next to it, then reuses them.</summary>
    /// <returns>The asynchronous cook.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task CookFolderPublishesInputActionsAndContextsInTheFileFolder()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteText("Content/Gameplay/Player.input.json", Player);
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(), api, compatibility);

        var result = await service.CookFolderAsync(AssetPath.Parse("/Content/Gameplay"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        _ = File.Exists(workspace.CookedPath("Content/Gameplay/Jump.oiact")).Should().BeTrue();
        _ = File.Exists(workspace.CookedPath("Content/Gameplay/Gameplay.oimap")).Should().BeTrue();
        var again = await service.CookFolderAsync(AssetPath.Parse("/Content/Gameplay"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = again.IsUpToDate.Should().BeTrue(
            $"status {again.Status}; cooked [{string.Join(", ", again.CookedAssets.Select(static asset => asset.CookedAssetUri))}]; "
            + string.Join("; ", again.Diagnostics.Select(static issue => issue.Code + ": " + (issue.TechnicalMessage ?? issue.Message))));
    }

    /// <summary>Two input files in one folder each own what they declared: the report says which file made each product.</summary>
    /// <returns>The asynchronous cook.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task CookFolderAttributesEachProductToTheFileThatDeclaredIt()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteText("Content/Gameplay/Player.input.json", Player);
        workspace.WriteText("Content/Gameplay/Vehicle.input.json", Player.Replace("Jump", "Boost", StringComparison.Ordinal).Replace("Gameplay", "Driving", StringComparison.Ordinal));
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(), api, compatibility);

        var result = await service.CookFolderAsync(AssetPath.Parse("/Content/Gameplay"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.TechnicalMessage ?? issue.Message)));
        _ = result.CookedAssets.ToDictionary(static asset => asset.CookedAssetUri.VirtualPath, static asset => asset.SourceAssetUri.VirtualPath).Should().BeEquivalentTo(new Dictionary<string, string>
        {
            ["/Content/Gameplay/Jump.oiact"] = "/Content/Gameplay/Player.input",
            ["/Content/Gameplay/Gameplay.oimap"] = "/Content/Gameplay/Player.input",
            ["/Content/Gameplay/Boost.oiact"] = "/Content/Gameplay/Vehicle.input",
            ["/Content/Gameplay/Driving.oimap"] = "/Content/Gameplay/Vehicle.input",
        });
    }

    /// <summary>A file that uses another file's action fails with the reason before the cooker runs.</summary>
    /// <returns>The asynchronous cook.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task CookFolderRejectsAnInputFileThatUsesAnUndeclaredAction()
    {
        using var workspace = new CookWorkspace(engineContent: EngineContentPak.Installed());
        workspace.WriteText("Content/Gameplay/Vehicle.input.json", Player.Replace("\"action\": \"Jump\"", "\"action\": \"Boost\"", StringComparison.Ordinal));
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(), api, compatibility);

        var result = await service.CookFolderAsync(AssetPath.Parse("/Content/Gameplay"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.IsPublished.Should().BeFalse();
        _ = result.Diagnostics.Should().Contain(static diagnostic => diagnostic.Code == ScriptDiagnosticCodes.InputReferenceUnsupported && diagnostic.Message.Contains("'Boost'", StringComparison.Ordinal));
    }
}
