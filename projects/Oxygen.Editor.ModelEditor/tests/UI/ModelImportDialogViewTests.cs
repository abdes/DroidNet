// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.Controls;
using Oxygen.Editor.ModelEditor.Importing;
using Oxygen.Editor.ModelEditor.Settings;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ModelEditor.TestSupport.ImportDialogs;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.ModelEditor.UI.Tests;

/// <summary>
/// The Import model dialog's content: every file selected when it opens, its list toolbar following the selection, the
/// settings panel built from the shared property editors, and the Import run's fields (model editor LLD section 7).
/// </summary>
[TestClass]
internal sealed class ModelImportDialogViewTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public Task TheDialogOpensWithEveryFileSelected() => EnqueueAsync(async () =>
    {
        var (project, sources) = CreateProject("Car.fbx", "Truck.glb");
        using var dialog = Create(project);
        _ = dialog.AddFiles(sources);
        var view = new ModelImportDialogView(dialog);

        await LoadTestContentAsync(view).ConfigureAwait(true);

        var list = (ListView)view.FindName("FileList");
        _ = list.SelectedItems.Should().HaveCount(2);
        _ = dialog.Selection.Should().HaveCount(2);
        _ = dialog.MoveUpCommand.CanExecute(parameter: null).Should().BeFalse("the selection holds the first file");
        _ = view.FindDescendant<ModelImportSettingsPanel>().Should().NotBeNull();
        _ = view.FindDescendants().OfType<PropertiesExpander>().Should().NotBeEmpty("the panel's groups are property sections");
    });

    [TestMethod]
    public Task WithoutFilesTheDialogShowsOnlyItsEmptyState() => EnqueueAsync(async () =>
    {
        var (project, sources) = CreateProject("Car.fbx", "Truck.glb");
        using var dialog = Create(project);
        var view = new ModelImportDialogView(dialog);
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var workspace = (FrameworkElement)view.FindName("Workspace");
        var list = (ListView)view.FindName("FileList");

        _ = workspace.Visibility.Should().Be(Visibility.Collapsed, "no empty list, settings panel or Before import shows");
        _ = view.FindDescendants().OfType<TextBlock>().Should().Contain(static text => text.Text == "Add models to import");

        _ = dialog.AddFiles([sources[0]]);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = workspace.Visibility.Should().Be(Visibility.Visible);
        _ = list.SelectedItems.Should().Equal(new object[] { dialog.Files[0] }, "the first file added shows selected, though the list loads only then");

        _ = dialog.AddFiles([sources[1]]);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = list.SelectedItems.Should().Equal(new object[] { dialog.Files[1] }, "a file added later is selected too");
        _ = dialog.Selection.Should().Equal(dialog.Files[1]);
    });

    [TestMethod]
    public Task AFileAddedWithThePickerInsideTheDialogIsSelected() => EnqueueAsync(async () =>
    {
        // As in the editor: the view in a content dialog, which loads its content late and unloads and loads it again,
        // Add files… focused in the empty state, and the picker answering later.
        var (project, sources) = CreateProject("Car.fbx", "Truck.glb");
        var picked = new Queue<string[]>([[sources[0]], [sources[1]]]);
        var dialogs = new Mock<IDialogService>();
        _ = dialogs.Setup(service => service.PickFilesAsync(It.IsAny<FilePickerSpec>(), It.IsAny<CancellationToken>()))
            .Returns(async () =>
            {
                await Task.Yield();
                return (IReadOnlyList<string>)picked.Dequeue();
            });
        using var model = Create(project, dialogs: dialogs.Object);
        var view = new ModelImportDialogView(model);
        var host = new Grid { Width = 1200, Height = 900 };
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var contentDialog = new ContentDialog { XamlRoot = host.XamlRoot, Content = view, CloseButtonText = "Cancel" };
        contentDialog.Resources["ContentDialogMaxWidth"] = 1120.0;
        var shown = contentDialog.ShowAsync();
        try
        {
            await WaitForRenderAsync().ConfigureAwait(true);
            var add = view.FindDescendants().OfType<Button>().First(static button => Equals(button.Content, "Add files…"));
            _ = add.Focus(FocusState.Keyboard);
            var list = (ListView)view.FindName("FileList");

            await model.AddFilesCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            _ = list.SelectedItems.Should().Equal(new object[] { model.Files[0] }, "the first file added shows selected");
            _ = model.Selection.Should().Equal(model.Files[0]);

            await model.AddFilesCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            _ = list.SelectedItems.Should().Equal(new object[] { model.Files[1] }, "a file added later shows selected");
            _ = model.Selection.Should().Equal(model.Files[1]);
        }
        finally
        {
            contentDialog.Hide();
            _ = await shown;
        }
    });

    [TestMethod]
    public Task WithFilesButNoSelectionTheSettingsAskForOne() => EnqueueAsync(async () =>
    {
        var (project, sources) = CreateProject("Car.fbx");
        using var dialog = Create(project);
        _ = dialog.AddFiles(sources);
        var view = new ModelImportDialogView(dialog);
        await LoadTestContentAsync(view).ConfigureAwait(true);

        ((ListView)view.FindName("FileList")).SelectedItems.Clear();
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = dialog.ShowsSelectionHint.Should().BeTrue();
        _ = view.FindDescendant<ModelImportSettingsPanel>()!.FindAscendant<ScrollViewer>()!.Visibility.Should().Be(Visibility.Collapsed);
        _ = view.FindDescendants().OfType<TextBlock>().Should().Contain(static text => text.Text == "Select files to see their settings." && text.Visibility == Visibility.Visible);
    });

    [TestMethod]
    public Task TheFactsBarSaysWhatWasFoundAndItsActionOpensBeforeImport() => EnqueueAsync(async () =>
    {
        var (project, sources) = CreateProject("Car.fbx", "Truck.glb");
        var findings = new Dictionary<string, ImmutableArray<DiagnosticRecord>>(StringComparer.Ordinal)
        {
            ["Car.fbx"] = [Finding(DiagnosticSeverity.Warning, "Animations are not imported."), Finding(DiagnosticSeverity.Warning, "Skins are not imported.")],
        };
        using var dialog = Create(project, findings: findings);
        _ = dialog.AddFiles(sources);
        dialog.UpdateSelection([dialog.Files[0]]);
        var view = new ModelImportDialogView(dialog);
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var bar = (InfoBar)view.FindName("FactsBar");
        var button = (Button)view.FindName("ReportButton");

        _ = (bar.Severity, bar.Title).Should().Be((InfoBarSeverity.Warning, "2 warnings"));
        _ = bar.ActionButton.Should().BeSameAs(button, "the report opens from the facts bar");
        _ = (button.Content, button.Visibility).Should().Be(("Before import", Visibility.Visible));
        _ = view.FindDescendants().OfType<DiagnosticCard>().Should().BeEmpty("the report is not part of the dialog's layout");

        var flyout = (Flyout)button.Flyout;
        flyout.AreOpenCloseAnimationsEnabled = false;
        flyout.ShowAt(button);
        try
        {
            await WaitForRenderAsync().ConfigureAwait(true);

            _ = ((FrameworkElement)flyout.Content).FindDescendants().OfType<DiagnosticCard>().Select(static card => card.Message)
                .Should().Equal("Animations are not imported.", "Skins are not imported.");
        }
        finally
        {
            flyout.Hide();
        }

        dialog.UpdateSelection([dialog.Files[1]]);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = (bar.Severity, bar.Title, button.Visibility).Should().Be((InfoBarSeverity.Informational, string.Empty, Visibility.Collapsed), "a file without findings has nothing to report");
    });

    [TestMethod]
    public Task SelectingAFileInTheListShowsItsName() => EnqueueAsync(async () =>
    {
        var (project, sources) = CreateProject("Car.fbx", "Truck.glb");
        using var dialog = Create(project);
        _ = dialog.AddFiles(sources);
        var view = new ModelImportDialogView(dialog);
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var list = (ListView)view.FindName("FileList");

        list.SelectedItems.Clear();
        list.SelectedItems.Add(dialog.Files[1]);
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = dialog.IsSingleSelection.Should().BeTrue();
        _ = dialog.SelectedName.Should().Be("Truck");
        _ = view.FindDescendants().OfType<TextBox>().Should().Contain(static box => box.Text == "Truck");
    });

    [TestMethod]
    public Task TheImportRunShowsEachPipelinesWorkersAndQueue() => EnqueueAsync(async () =>
    {
        var (project, _) = CreateProject();
        using var dialog = Create(project);
        var view = new ImportRunView(dialog.Run);

        await LoadTestContentAsync(view).ConfigureAwait(true);

        _ = view.FindDescendants().OfType<NumberField>().Should().HaveCount(2, "Threads and Jobs at once are number fields");
        _ = view.FindDescendants().OfType<NumberBox>().Count().Should().BeGreaterThanOrEqualTo(2 * dialog.Run.Pipelines.Length);
    });
}
