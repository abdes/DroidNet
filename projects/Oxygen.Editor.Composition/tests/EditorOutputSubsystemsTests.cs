// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.Diagnostics;

namespace Oxygen.Editor.Composition.UI.Tests;

/// <summary>The editor's Output panel attributes each log source to the feature area a user recognizes.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Diagnostics")]
public sealed class EditorOutputSubsystemsTests
{
    /// <summary>Real logging sources of each area resolve to that area's subsystem.</summary>
    /// <param name="source">The source of an output entry: a logger's type name or a direct source.</param>
    /// <param name="expected">The subsystem it resolves to.</param>
    [TestMethod]
    [DataRow("Oxygen.Engine", EditorOutputSubsystems.Engine)]
    [DataRow("Oxygen.Editor.Runtime.Engine.EngineService", EditorOutputSubsystems.Engine)]
    [DataRow("Player", EditorOutputSubsystems.Player)]
    [DataRow("Oxygen.Editor.Runtime.Player.PlayerProcess", EditorOutputSubsystems.Player)]
    [DataRow("Oxygen.Editor.LivePreview.Play.PlaySession", EditorOutputSubsystems.Player)]
    [DataRow("Oxygen.Editor.LivePreview.Session.PreviewSession", EditorOutputSubsystems.Preview)]
    [DataRow("Oxygen.Editor.ContentPipeline.Cooking.AutomaticCookService", EditorOutputSubsystems.Cooker)]
    [DataRow("Oxygen.Editor.ContentPipeline.Publication.CookedPublisher", EditorOutputSubsystems.Cooker)]
    [DataRow("Oxygen.Editor.ContentPipeline.ContentCookCoordinator", EditorOutputSubsystems.Cooker)]
    [DataRow("Oxygen.Editor.ContentPipeline.MaterialCookService", EditorOutputSubsystems.Cooker)]
    [DataRow("Oxygen.Editor.ContentPipeline.ImportToolContentPipelineApi", EditorOutputSubsystems.Cooker)]
    [DataRow("Oxygen.Editor.Cooking.CookingPanelViewModel", EditorOutputSubsystems.Cooker)]
    [DataRow("Oxygen.Editor.CookedInspector.CookedInspectionViewModel", EditorOutputSubsystems.Cooker)]
    [DataRow("Oxygen.Editor.ContentPipeline.Relocation.AssetMover", EditorOutputSubsystems.Assets)]
    [DataRow("Oxygen.Editor.ContentPipeline.ContentPipelineModule", EditorOutputSubsystems.Assets)]
    [DataRow("Oxygen.Editor.ContentExplorer.ContentExplorerViewModel", EditorOutputSubsystems.Assets)]
    [DataRow("Oxygen.Managed.Assets.Catalog.FileSystem.FileSystemCatalog", EditorOutputSubsystems.Assets)]
    [DataRow("Oxygen.Editor.MaterialEditor.MaterialEditorViewModel", EditorOutputSubsystems.Materials)]
    [DataRow("Oxygen.Editor.SceneEditor.SceneEditorViewModel", EditorOutputSubsystems.Scene)]
    [DataRow("Oxygen.Editor.SceneInspector.Components.Transform.TransformViewModel", EditorOutputSubsystems.Scene)]
    [DataRow("Oxygen.Editor.Scripting.ScriptAnalysis", EditorOutputSubsystems.Scripting)]
    [DataRow("Oxygen.Editor.Input.InputBindings", EditorOutputSubsystems.Input)]
    [DataRow("Oxygen.Editor.Sessions.ProjectSessionHost", EditorOutputSubsystems.Project)]
    [DataRow("Oxygen.Editor.Shell.Documents.DocumentHost", EditorOutputSubsystems.Documents)]
    [DataRow("DroidNet.Aura.Documents.DocumentService", EditorOutputSubsystems.Documents)]
    [DataRow("DroidNet.Aura.Windowing.WindowManagerService", EditorOutputSubsystems.Layout)]
    [DataRow("DroidNet.Docking.Workspace.DockingWorkspace", EditorOutputSubsystems.Layout)]
    [DataRow("DroidNet.Routing.Router", EditorOutputSubsystems.Router)]
    [DataRow("DroidNet.Config.SettingsService", EditorOutputSubsystems.Settings)]
    [DataRow("DroidNet.Aura.Theming.AppThemeModeService", EditorOutputSubsystems.Settings)]
    [DataRow("Oxygen.Editor.Output.PlayerLogOutput", EditorOutputSubsystems.Editor)]
    [DataRow("Oxygen.Editor.Problems.ProblemsViewModel", EditorOutputSubsystems.Editor)]
    [DataRow("DroidNet.Controls.DynamicTree", EditorOutputSubsystems.Editor)]
    [DataRow("DroidNet.Bootstrap.Bootstrapper", EditorOutputSubsystems.Editor)]
    [DataRow("OperationResult", EditorOutputSubsystems.Editor)]
    public void Map_ResolvesEachAreasSources(string source, string expected)
        => _ = EditorOutputSubsystems.Map.Resolve(source).Should().Be(expected);

    /// <summary>A source outside the editor's own code has no subsystem.</summary>
    [TestMethod]
    public void Map_LeavesThirdPartySourcesUnmapped()
        => _ = EditorOutputSubsystems.Map.Resolve("Microsoft.Hosting.Lifetime").Should().BeNull();
}
