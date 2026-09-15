// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.World.Cooking;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises retry from the Cooking panel after the imported source has already been retained.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>A failed native job retains source and diagnostics; Retry works after the original external file is removed.</summary>
    /// <param name="format">The retained source format.</param>
    /// <returns>The import failure/retry journey.</returns>
    [TestMethod]
    [DataRow("gltf")]
    [DataRow("fbx")]
    public Task CookingPanelRetriesRetainedNativeImportWithoutOriginalFile(string format) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene => AddGeometryNode(scene, "Cube"));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(40));
        using var services = new CatalogWorkloadServices(fixture, new FailFirstImportProcess());
        using var catalog = new ProjectAssetCatalog(services.Projects, services.Storage, services.Builtins);
        using var provider = new ContentBrowserAssetProvider(catalog, services.Projects, services.Scopes, new AssetIdentityReducer(), services.Pipeline, services.Documents, services.Runs, fixture.Runtime);
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        using var publication = fixture.RegisterWorkspacePublication(services, catalog);
        using var panel = new CookingPanelViewModel(services.Runs, services.Pipeline, services.Projects, Mock.Of<ICookingWorkspaceActions>(), CreateStatusHosting());
        var view = new CookingPanelView { ViewModel = panel };
        var root = new Grid { Width = 900, Height = 650, RequestedTheme = ElementTheme.Dark, Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(Microsoft.UI.Colors.Black), Children = { view } };
        await LoadTestContentAsync(root).ConfigureAwait(true);
        var window = DroidNet.Tests.VisualUserInterfaceTestsApp.MainWindow.AppWindow;
        var size = window.Size;
        window.Resize(new((int)(root.Width * root.XamlRoot.RasterizationScale) + 40, (int)(root.Height * root.XamlRoot.RasterizationScale) + 80));
        using var browser = CreateNativeImportBrowser(services, provider);
        var path = await WriteTypedModelSourceAsync(fixture, format, timeout.Token).ConfigureAwait(true);
        try
        {
            await DriveNativeImportDialogAsync(browser, services, root.XamlRoot, path, replace: false, accept: true, timeout.Token).ConfigureAwait(true);
            var failed = await WaitForImportPanelAsync(panel, services, 1, timeout.Token, succeeds: false).ConfigureAwait(true);
            _ = failed.Messages.Should().Contain(message => message.Text.Contains(FailFirstImportProcess.Failure, StringComparison.Ordinal));
            _ = failed.Request.IsReimport.Should().BeTrue();
            _ = failed.Request.Import.Should().BeNull();
            _ = Directory.Exists(Path.Combine(fixture.ProjectRoot, ".cooked/Content")).Should().BeFalse();
            _ = File.Exists(Path.Combine(fixture.ProjectRoot, "Content/SourceMedia/DCC/ReviewedTriangle/Triangle." + format)).Should().BeTrue();
            File.Delete(path);
            await this.CaptureComponentLayoutAsync(root, "native-import-failure-" + format + ".png").ConfigureAwait(true);
            InvokeImportButton(view.FindDescendant<Button>(button => string.Equals(button.Content as string, "Retry", StringComparison.Ordinal))!);
            var retried = await WaitForImportPanelAsync(panel, services, 2, timeout.Token).ConfigureAwait(true);
            _ = retried.Request.ScopeUri.Should().Be(failed.Request.ScopeUri);
            var material = retried.Assets.Values.Single(asset => asset.Kind == ContentCookAssetKind.Material).AssetUri;
            var node = fixture.Source.RootNodes[0].Id;
            _ = (await fixture.Commands.EditMaterialSlotAsync(fixture.Context, [node], 0, material, EditSessionToken.OneShot).ConfigureAwait(true)).Succeeded.Should().BeTrue();
            _ = await WaitForNodeAsync(fixture, node, value => value.MaterialBaseColors.Length == 1 && Vector4.Distance(value.MaterialBaseColors[0], new(0.8f, 0.2f, 0.1f, 1)) < 0.001f, timeout.Token).ConfigureAwait(true);
        }
        finally
        {
            FindImportDialog(root.XamlRoot)?.Hide();
            await UnloadTestContentAsync(root).ConfigureAwait(true);
            window.Resize(size);
        }
    });

    private sealed class FailFirstImportProcess : IContentPipelineProcessRunner
    {
        public const string Failure = "Injected importer failure after source retention.";
        private readonly ContentPipelineProcessRunner inner = new();
        private bool failed;

        public Task<ContentPipelineProcessResult> RunAsync(ContentPipelineProcessRequest request, CancellationToken cancellationToken)
        {
            if (!this.failed && request.Arguments.Contains("--manifest", StringComparer.Ordinal))
            {
                this.failed = true;
                request.Output?.Report(new(Failure, IsStandardError: true));
                return Task.FromResult(new ContentPipelineProcessResult(1, string.Empty, Failure));
            }

            return this.inner.RunAsync(request, cancellationToken);
        }
    }
}
