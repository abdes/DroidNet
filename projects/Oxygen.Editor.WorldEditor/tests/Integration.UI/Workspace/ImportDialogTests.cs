// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Moq;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.World.Cooking;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.TestSupport;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.WorldEditor.TestSupport.ModelImportScenario;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneAssertions;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneData;
using static Oxygen.Editor.WorldEditor.TestSupport.PublicationWorkflows;

namespace Oxygen.Editor.WorldEditor.Integration.UI.Tests.Workspace;

[TestClass]
public sealed partial class ImportDialogTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Cancel preserves files; explicit import/replacement publishes the reviewed values and updates an existing consumer.</summary>
    /// <param name="format">The qualified source format.</param>
    /// <returns>The complete dialog and native publication journey.</returns>
    [TestMethod]
    [DataRow("gltf")]
    [DataRow("fbx")]
    public Task NativeImportDialogCancelAndReplacementPreserveReviewedIntent(string format) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene => AddGeometryNode(scene, "Cube"));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        using var services = new CatalogWorkloadServices(fixture);
        await using var catalog = new ProjectAssetCatalog(services.Projects, services.Storage, services.Builtins, services.Publication);
        using var provider = new ContentBrowserAssetProvider(catalog, services.Projects, services.Scopes, new AssetIdentityReducer(), services.Pipeline, services.Documents, services.Runs, fixture.Runtime);
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        using var publication = fixture.RegisterWorkspacePublication(services, catalog);
        using var panel = new CookingPanelViewModel(services.Runs, services.Pipeline, services.Projects, Mock.Of<ICookingWorkspaceActions>(), CreateStatusHosting());
        var root = new Grid
        {
            Width = 900,
            Height = 650,
            RequestedTheme = ElementTheme.Dark,
            Background = new SolidColorBrush(Microsoft.UI.Colors.Black),
        };
        root.Children.Add(new CookingPanelView { ViewModel = panel });
        await LoadTestContentAsync(root).ConfigureAwait(true);
        var window = VisualUserInterfaceTestsApp.MainWindow.AppWindow;
        var size = window.Size;
        window.Resize(new((int)(root.Width * root.XamlRoot.RasterizationScale) + 40, (int)(root.Height * root.XamlRoot.RasterizationScale) + 80));
        using var browser = CreateNativeImportBrowser(services, provider);
        var path = await WriteTypedModelSourceAsync(fixture, format, timeout.Token).ConfigureAwait(true);
        try
        {
            await DriveNativeImportDialogAsync(browser, services, root.XamlRoot, path, replace: false, accept: false, timeout.Token).ConfigureAwait(true);
            _ = services.Runs.Runs.Should().BeEmpty();
            _ = Directory.Exists(Path.Combine(fixture.ProjectRoot, "Content/SourceMedia/DCC/ReviewedTriangle")).Should().BeFalse();
            await DriveNativeImportDialogAsync(browser, services, root.XamlRoot, path, replace: false, accept: true, timeout.Token).ConfigureAwait(true);
            var first = await WaitForImportPanelAsync(panel, services, 1, timeout.Token).ConfigureAwait(true);
            await CheckNativeReplacementAsync(fixture, services, browser, panel, root.XamlRoot, first, path, format, timeout.Token).ConfigureAwait(true);
        }
        finally
        {
            FindImportDialog(root.XamlRoot)?.Hide();
            await UnloadTestContentAsync(root).ConfigureAwait(true);
            window.Resize(size);
        }
    });

    private static async Task CheckNativeReplacementAsync(NativeSceneFixture fixture, CatalogWorkloadServices services, AssetsViewModel browser, CookingPanelViewModel panel, XamlRoot root, CookRunSnapshot first, string path, string format, CancellationToken cancellationToken)
    {
        var material = first.Assets.Values.Single(asset => asset.Kind == ContentCookAssetKind.Material).AssetUri;
        var node = fixture.Source.RootNodes[0].Id;
        _ = (await fixture.Commands.EditMaterialSlotAsync(fixture.Context, [node], await fixture.ReadSingleMaterialSlotAsync(node, cancellationToken).ConfigureAwait(true), material, EditSessionToken.OneShot).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        var before = await WaitForNodeAsync(fixture, node, value => value.MaterialBaseColors.Length == 1 && Vector4.Distance(value.MaterialBaseColors[0], new(0.8f, 0.2f, 0.1f, 1)) < 0.001f, cancellationToken).ConfigureAwait(true);
        var revision = fixture.Context.Metadata.ChangeVersion;
        var history = fixture.Context.History.UndoStack.Count;
        var retained = Path.Combine(fixture.ProjectRoot, "Content/SourceMedia/DCC/ReviewedTriangle/Triangle." + format);
        _ = File.Exists(Path.Combine(Path.GetDirectoryName(retained)!, "static_textured_checker.png")).Should().BeTrue();
        var original = await File.ReadAllBytesAsync(retained, cancellationToken).ConfigureAwait(true);
        var published = ReadPublishedHashes(fixture.ProjectRoot);
        var text = await File.ReadAllTextAsync(path, cancellationToken).ConfigureAwait(true);
        if (string.Equals(format, "gltf", StringComparison.Ordinal))
        {
            var source = JsonNode.Parse(text)!;
            source["materials"]![0]!["pbrMetallicRoughness"]!["baseColorFactor"] = new JsonArray(0.1f, 0.8f, 0.2f, 1f);
            text = source.ToJsonString();
        }
        else
        {
            text = text.Replace("0.8,0.2,0.1", "0.1,0.8,0.2", StringComparison.Ordinal);
        }

        await File.WriteAllTextAsync(path, text, cancellationToken).ConfigureAwait(true);
        await DriveNativeImportDialogAsync(browser, services, root, path, replace: true, accept: false, cancellationToken).ConfigureAwait(true);
        _ = services.Runs.Runs.Should().ContainSingle();
        _ = (await File.ReadAllBytesAsync(retained, cancellationToken).ConfigureAwait(true)).Should().Equal(original);
        _ = ReadPublishedHashes(fixture.ProjectRoot).Should().BeEquivalentTo(published);
        await DriveNativeImportDialogAsync(browser, services, root, path, replace: true, accept: true, cancellationToken).ConfigureAwait(true);
        var replacement = await WaitForImportPanelAsync(panel, services, 2, cancellationToken).ConfigureAwait(true);
        _ = replacement.ImportedOutputs.Should().BeEquivalentTo(first.ImportedOutputs);
        _ = (await File.ReadAllBytesAsync(retained, cancellationToken).ConfigureAwait(true)).Should().Equal(await File.ReadAllBytesAsync(path, cancellationToken).ConfigureAwait(true));
        var after = await fixture.ReadNodeAsync(node, cancellationToken).ConfigureAwait(true);
        _ = after.MaterialKeys.Should().Equal(before.MaterialKeys);
        _ = after.MaterialBaseColors.Should().ContainSingle().Which.Should().Be(new Vector4(0.1f, 0.8f, 0.2f, 1));
        _ = fixture.Context.Metadata.ChangeVersion.Should().Be(revision);
        _ = fixture.Context.History.UndoStack.Should().HaveCount(history);
        _ = fixture.Context.Metadata.IsDirty.Should().BeTrue();
    }
}
