// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.SavedSourceScenario;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Cooking;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class PreviewCookTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Preview demand cannot silently become a whole-scene cook or cook a built-in.</summary>
    /// <param name="identity">The non-authorable preview scope.</param>
    /// <returns>The asynchronous scope regression.</returns>
    [TestMethod]
    [DataRow("asset:///Content/Scenes/Main.oscene.json")]
    [DataRow("asset://Engine/Generated/BasicShapes/Cylinder")]
    [DataRow("asset://Engine/Generated/Materials/Default")]
    [DataRow("asset:///Content/Models/Source.fbx")]
    public async Task PreviewDemandRejectsNonAssetScopes(string identity)
    {
        using var workspace = new CookWorkspace();
        var pipeline = CreateService(workspace, new CapturingSceneDescriptorGenerator([]), CreateSuccessfulApi(workspace, ControlledSourceAnalysis.AnalyzeAsync));
        Func<Task> request = () => pipeline.CookPreviewAssetAsync(new(identity), workspace.ProjectContext, this.TestContext.CancellationToken);
        _ = await request.Should().ThrowAsync<ArgumentException>().ConfigureAwait(false);
        _ = workspace.CookCoordinator.Runs.Should().BeEmpty();
    }
}
