// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Moq;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Importing;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Cooking;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Tests;

/// <summary>Connects the actual import/replacement dialogs to native cooking and live material publication.</summary>
public sealed partial class InspectorControlTests
{
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
        using var catalog = new ProjectAssetCatalog(services.Projects, services.Storage, services.Builtins);
        using var provider = new ContentBrowserAssetProvider(catalog, services.Projects, services.Scopes, new AssetIdentityReducer(), services.Pipeline, services.Documents, services.Runs, fixture.Runtime);
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        using var publication = fixture.RegisterWorkspacePublication(services, catalog);
        using var panel = new CookingPanelViewModel(services.Runs, services.Pipeline, services.Projects, Mock.Of<ICookingWorkspaceActions>(), CreateStatusHosting());
        var root = new Grid { Width = 900, Height = 650, RequestedTheme = ElementTheme.Dark, Background = new SolidColorBrush(Microsoft.UI.Colors.Black) };
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
            await this.CaptureComponentLayoutAsync(root, "native-import-result-" + format + ".png").ConfigureAwait(true);
        }
        finally
        {
            FindImportDialog(root.XamlRoot)?.Hide();
            await UnloadTestContentAsync(root).ConfigureAwait(true);
            window.Resize(size);
        }
    });

    private static AssetsViewModel CreateNativeImportBrowser(CatalogWorkloadServices services, ContentBrowserAssetProvider provider)
    {
        var window = new Mock<IManagedWindow>();
        _ = window.SetupGet(value => value.Window).Returns(VisualUserInterfaceTestsApp.MainWindow);
        _ = window.SetupGet(value => value.DispatcherQueue).Returns(VisualUserInterfaceTestsApp.DispatcherQueue);
        var windows = new Mock<IWindowManagerService>();
        _ = windows.SetupGet(value => value.ActiveWindow).Returns(window.Object);
        var dialogs = new DialogService(windows.Object);
        var locator = new Mock<IViewLocator>();
        _ = locator.Setup(value => value.ResolveView(It.IsAny<object>())).Returns(() => new SceneImportDialogView());
        var state = new ContentBrowserState(services.Projects);
        state.SetSelectedFolders(["/Content/Models"]);
        return new(services.Runs, new ViewModelToView(locator.Object), state, services.Projects, Mock.Of<IProjectManagerService>(), Mock.Of<IAuthoringTargetResolver>(), services.Pipeline, provider, Mock.Of<IOperationResultPublisher>(), new OperationStatusReducer(), services.Storage, new StrongReferenceMessenger(), dialogs, windows.Object);
    }

    private static ContentDialog? FindImportDialog(XamlRoot root)
        => VisualTreeHelper.GetOpenPopupsForXamlRoot(root).Select(static popup => popup.Child is ContentDialog dialog ? dialog : popup.Child?.FindDescendant<ContentDialog>()).FirstOrDefault(static dialog => dialog is not null);

    private static void InvokeImportButton(Button button)
    {
        _ = button.IsEnabled.Should().BeTrue();
        _ = button.Focus(FocusState.Programmatic);
        var peer = FrameworkElementAutomationPeer.FromElement(button) ?? new ButtonAutomationPeer(button);
        ((IInvokeProvider)peer.GetPattern(PatternInterface.Invoke)).Invoke();
    }

    private static async Task DriveNativeImportDialogAsync(AssetsViewModel browser, CatalogWorkloadServices services, XamlRoot root, string sourcePath, bool replace, bool accept, CancellationToken cancellationToken)
    {
        var operation = browser.ImportSourceFileAsync(services.Projects.ActiveProject!, sourcePath);
        ContentDialog? dialog = null;
        try
        {
            var started = System.Diagnostics.Stopwatch.StartNew();
            while ((dialog = FindImportDialog(root))?.Content is not SceneImportDialogView { IsLoaded: true })
            {
                if (operation.IsCompleted)
                {
                    await operation.ConfigureAwait(true);
                    Assert.Fail("Import completed without an open review dialog.");
                }

                _ = started.Elapsed.Should().BeLessThan(TimeSpan.FromSeconds(10), "the review content must load; dialog found: {0}", dialog is not null);
                await Task.Delay(20, cancellationToken).ConfigureAwait(true);
            }

            var view = (SceneImportDialogView)dialog.Content;
            var name = view.FindDescendant<TextBox>(item => string.Equals(AutomationProperties.GetAutomationId(item), "ModelImportName", StringComparison.Ordinal))!;
            var destination = view.FindDescendant<TextBox>(item => string.Equals(AutomationProperties.GetAutomationId(item), "ModelImportDestination", StringComparison.Ordinal))!;
            name.Text = "../invalid";
            _ = dialog.IsPrimaryButtonEnabled.Should().BeFalse();
            name.Text = "ReviewedTriangle";
            destination.Text = "/Content/Models";
            await WaitForRenderAsync().WaitAsync(cancellationToken).ConfigureAwait(true);
            _ = view.ViewModel!.HasCollision.Should().Be(replace);
            if (replace)
            {
                _ = dialog.IsPrimaryButtonEnabled.Should().BeFalse();
                InvokeImportButton(view.FindDescendant<Button>(item => string.Equals(AutomationProperties.GetAutomationId(item), "ModelImportReviewReplacement", StringComparison.Ordinal))!);
                while (view.ViewModel.Replacement is null)
                {
                    await Task.Delay(20, cancellationToken).ConfigureAwait(true);
                }
            }

            _ = dialog.IsPrimaryButtonEnabled.Should().BeTrue();
            _ = dialog.PrimaryButtonText.Should().Be(replace ? "Replace and import" : "Import");
            var label = accept ? dialog.PrimaryButtonText : dialog.CloseButtonText;
            InvokeImportButton(dialog.FindDescendant<Button>(button => string.Equals(button.Name, accept ? "PrimaryButton" : "CloseButton", StringComparison.Ordinal)
                && string.Equals(button.Content as string, label, StringComparison.Ordinal))!);
            await operation.WaitAsync(cancellationToken).ConfigureAwait(true);
        }
        finally
        {
            dialog?.Hide();
            await operation.WaitAsync(TimeSpan.FromSeconds(10), CancellationToken.None).ConfigureAwait(true);
        }
    }

    private static async Task<CookRunSnapshot> WaitForImportPanelAsync(CookingPanelViewModel panel, CatalogWorkloadServices services, int expectedCount, CancellationToken cancellationToken, bool succeeds = true)
    {
        while (panel.SelectedRun?.Snapshot is not { IsCompleted: true } || panel.Runs.Count != expectedCount || services.Runs.Runs.Count != expectedCount)
        {
            await Task.Delay(20, cancellationToken).ConfigureAwait(true);
        }

        var run = panel.SelectedRun.Snapshot;
        if (succeeds)
        {
            _ = run.State.Should().BeOneOf(CookRunState.Succeeded, CookRunState.SucceededWithWarnings);
            _ = run.ImportedOutputs.Should().HaveCount(3);
        }
        else
        {
            _ = run.State.Should().Be(CookRunState.Failed);
            _ = run.ImportedOutputs.Should().BeEmpty();
        }

        _ = run.Messages.Should().NotBeEmpty();
        return run;
    }

    private static async Task CheckNativeReplacementAsync(NativeSceneFixture fixture, CatalogWorkloadServices services, AssetsViewModel browser, CookingPanelViewModel panel, XamlRoot root, CookRunSnapshot first, string path, string format, CancellationToken cancellationToken)
    {
        var material = first.Assets.Values.Single(asset => asset.Kind == ContentCookAssetKind.Material).AssetUri;
        var node = fixture.Source.RootNodes[0].Id;
        _ = (await fixture.Commands.EditMaterialSlotAsync(fixture.Context, [node], 0, material, EditSessionToken.OneShot).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        var before = await WaitForNodeAsync(fixture, node, value => value.MaterialBaseColors.Length == 1 && Vector4.Distance(value.MaterialBaseColors[0], new(0.8f, 0.2f, 0.1f, 1)) < 0.001f, cancellationToken).ConfigureAwait(true);
        var revision = fixture.Context.Metadata.ChangeVersion;
        var history = fixture.Context.History.UndoStack.Count;
        var retained = Path.Combine(fixture.ProjectRoot, "Content/SourceMedia/DCC/ReviewedTriangle/Triangle." + format);
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
