// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

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
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Cooking;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal sealed class ModelImportScenario : DroidNet.Tests.VisualUserInterfaceTests
{
    internal static AssetsViewModel CreateNativeImportBrowser(CatalogWorkloadServices services, ContentBrowserAssetProvider provider)
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

    internal static ContentDialog? FindImportDialog(XamlRoot root) => VisualTreeHelper.GetOpenPopupsForXamlRoot(root).Select(static popup => popup.Child is ContentDialog dialog ? dialog : popup.Child?.FindDescendant<ContentDialog>()).FirstOrDefault(static dialog => dialog is not null);

    internal static void InvokeImportButton(Button button)
    {
        _ = button.IsEnabled.Should().BeTrue();
        _ = button.Focus(FocusState.Programmatic);
        var peer = FrameworkElementAutomationPeer.FromElement(button) ?? new ButtonAutomationPeer(button);
        ((IInvokeProvider)peer.GetPattern(PatternInterface.Invoke)).Invoke();
    }

    internal static async Task DriveNativeImportDialogAsync(AssetsViewModel browser, CatalogWorkloadServices services, XamlRoot root, string sourcePath, bool replace, bool accept, CancellationToken cancellationToken)
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
            InvokeImportButton(dialog.FindDescendant<Button>(button => string.Equals(button.Name, accept ? "PrimaryButton" : "CloseButton", StringComparison.Ordinal) && string.Equals(button.Content as string, label, StringComparison.Ordinal))!);
            await operation.WaitAsync(cancellationToken).ConfigureAwait(true);
        }
        finally
        {
            dialog?.Hide();
            await operation.WaitAsync(TimeSpan.FromSeconds(10), CancellationToken.None).ConfigureAwait(true);
        }
    }

    internal static async Task<CookRunSnapshot> WaitForImportPanelAsync(CookingPanelViewModel panel, CatalogWorkloadServices services, int expectedCount, CancellationToken cancellationToken, bool succeeds = true)
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
            _ = run.Messages.Should().NotContain(message => message.Text.Contains('\u001b'));
            _ = run.Messages.Should().Contain(message => message.Text.Contains("geom-buffer:", StringComparison.Ordinal) && message.Text.Length > 80 && !message.Text.EndsWith("...", StringComparison.Ordinal));
        }
        else
        {
            _ = run.State.Should().Be(CookRunState.Failed);
            _ = run.ImportedOutputs.Should().BeEmpty();
        }

        _ = run.Messages.Should().NotBeEmpty();
        return run;
    }

    internal static async Task<ContentCookResult> ImportTypedModelAsync(NativeSceneFixture fixture, CatalogWorkloadServices services, string format, CancellationToken cancellationToken)
    {
        var path = await WriteTypedModelSourceAsync(fixture, format, cancellationToken).ConfigureAwait(true);
        var result = await services.Pipeline.ImportSourceAsync(new(services.Projects.ActiveProject!, path, "Triangle", new("asset:///Content/Models")), cancellationToken).ConfigureAwait(true);
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.Message)));
        _ = result.CookedAssets.Should().Contain(asset => asset.Kind == ContentCookAssetKind.Scene);
        return result;
    }

    internal static async Task<string> WriteTypedModelSourceAsync(NativeSceneFixture fixture, string format, CancellationToken cancellationToken)
    {
        var path = Path.Combine(fixture.ProjectRoot, "Incoming", "Triangle." + format);
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var source = await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures/static_textured_triangle." + format), cancellationToken).ConfigureAwait(true);
        if (string.Equals(format, "fbx", StringComparison.Ordinal))
        {
            source = source.Replace("\"DiffuseColor\", \"Color\", \"\", \"A\",1,1,1", "\"DiffuseColor\", \"Color\", \"\", \"A\",0.8,0.2,0.1", StringComparison.Ordinal);
        }

        File.Copy(Path.Combine(AppContext.BaseDirectory, "Fixtures/static_textured_checker.png"), Path.Combine(Path.GetDirectoryName(path)!, "static_textured_checker.png"));
        await File.WriteAllTextAsync(path, source, cancellationToken).ConfigureAwait(true);
        return path;
    }

    internal static async Task<ContentCookResult> CreateImportedLibraryAsync(NativeSceneFixture consumer, CatalogWorkloadServices services, string format, CancellationToken cancellationToken)
    {
        var producer = new NativeSceneFixture(automatic: false);
        await using var lifetime = producer.ConfigureAwait(true);
        using var producerServices = new CatalogWorkloadServices(producer);
        var imported = await ImportTypedModelAsync(producer, producerServices, format, cancellationToken).ConfigureAwait(true);
        var source = await producer.GetCookedRootAsync(producer.ProjectRoot, cancellationToken).ConfigureAwait(true);
        var library = Path.Combine(consumer.ProjectRoot, "Library");
        foreach (var path in Directory.GetFiles(source, "*", SearchOption.AllDirectories))
        {
            if (string.Equals(Path.GetFileName(path), ".generation.lock", StringComparison.Ordinal))
            {
                continue;
            }

            var target = Path.Combine(library, Path.GetRelativePath(source, path));
            _ = Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            File.Copy(path, target);
        }

        var next = services.Projects.ActiveProject! with
        {
            LocalFolderMounts = [new("Library", library)]
        };
        var manager = new Oxygen.Editor.Projects.ProjectManagerService(services.Storage);
        var saved = await manager.LoadProjectInfoAsync(consumer.ProjectRoot).ConfigureAwait(true) ?? throw new InvalidDataException("The consumer project must be saved.");
        var updated = new Oxygen.Editor.Projects.ProjectInfo(next.ProjectId, next.Name, next.Category, next.ProjectRoot, next.Thumbnail)
        {
            AuthoringMounts = [.. next.AuthoringMounts],
            LocalFolderMounts = [.. next.LocalFolderMounts],
            CookedContentOrder = [.. next.CookedContentOrder],
        };
        await manager.SaveProjectInfoAsync(updated, saved, cancellationToken).ConfigureAwait(true);
        services.Projects.Activate(next);
        return imported;
    }
}
