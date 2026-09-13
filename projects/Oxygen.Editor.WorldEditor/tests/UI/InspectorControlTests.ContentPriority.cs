// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ContentBrowser.ProjectExplorer;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks the compact priority editor and the meaning of its ordering controls.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Higher rows win, move actions preserve selection, and the committed order is the inverse display order.</summary>
    /// <param name="light">Whether to render the light theme.</param>
    /// <returns>The asynchronous priority-control regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task ContentPriorityControlsPreserveSelectionAndExplainTheOrder(bool light) => EnqueueAsync(async () =>
    {
        var directory = Directory.CreateTempSubdirectory("Oxygen-PriorityControls-");
        try
        {
            var first = Directory.CreateDirectory(Path.Combine(directory.FullName, "First")).FullName;
            var second = Directory.CreateDirectory(Path.Combine(directory.FullName, "Second")).FullName;
            await File.WriteAllBytesAsync(Path.Combine(first, "container.index.bin"), [], this.TestContext.CancellationToken).ConfigureAwait(true);
            await File.WriteAllBytesAsync(Path.Combine(second, "container.index.bin"), [], this.TestContext.CancellationToken).ConfigureAwait(true);
            var project = new ProjectContext
            {
                ProjectId = Guid.NewGuid(), Name = "Priority", Category = Category.Games, ProjectRoot = directory.FullName,
                AuthoringMounts = [new("Content", "Content")], LocalFolderMounts = [new("First", first), new("Second", second)], Scenes = [],
            };
            var model = new ContentPriorityViewModel(project);
            var view = new ContentPriorityView { ViewModel = model };
            var root = new Grid { Width = 440, Height = 280, RequestedTheme = light ? ElementTheme.Light : ElementTheme.Dark };
            root.Children.Add(view);
            await LoadTestContentAsync(root).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = model.Sources.Select(static item => item.Name).Should().Equal("Project output", "Second", "First");
            var toolbar = view.FindDescendant<ToolBar>()!;
            _ = toolbar.OverflowButtonVisibility.Should().Be(Visibility.Collapsed);
            _ = ToolTipService.GetToolTip(toolbar.PrimaryItems.OfType<ToolBarButton>().First()).Should().Be("Increase priority");
            var list = view.FindDescendant<ListView>()!;
            list.SelectedIndex = 1;
            var selected = model.SelectedSource;
            model.IncreasePriorityCommand.Execute(parameter: null);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = model.SelectedSource.Should().BeSameAs(selected);
            _ = list.SelectedItem.Should().BeSameAs(selected);
            _ = model.GetMountOrder().Select(static source => source.Name).Should().Equal("First", null, "Second");
            _ = model.IncreasePriorityCommand.CanExecute(parameter: null).Should().BeFalse();
            await this.CaptureQueryLayoutAsync(root, "content-priority-" + light + ".png").ConfigureAwait(true);
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    });
}
