// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Docking;
using DroidNet.Routing.Debugger.UI.Docks;
using DroidNet.Routing.View;
using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

/// <summary>
/// A layout strategy for the workspace.
/// </summary>
public sealed partial class WorkSpaceLayout(IDocker docker, IViewLocator viewLocator, ILogger logger)
    : ObservableObject
{
    private readonly Stack<VectorGrid> grids = new();

    public VectorGrid UpdateContent()
    {
        var orientation = docker.Root.Orientation == DockGroupOrientation.Vertical
            ? Orientation.Vertical
            : Orientation.Horizontal;
        Debug.WriteLine($"Start a new {orientation} VG for the root dock");
        var grid = new VectorGrid(orientation);
        this.PushGrid(grid, "root");
        this.Layout(docker.Root);
        Debug.Assert(this.grids.Count == 1, "some pushes to the grids stack were not matched by a corresponding pop");
        _ = this.grids.Pop();

        return grid;
    }

    private static bool ShouldShowGroup(IDockGroup? group)
    {
        if (group is null)
        {
            return false;
        }

        if (!group.IsEmpty)
        {
            return group is IDockTray || group.Docks.Any(d => d.State != DockingState.Minimized);
        }

        return ShouldShowGroup(group.First) || ShouldShowGroup(group.Second);
    }

    private static bool ShouldStretch(IDockGroup group)
    {
        if (group.IsCenter)
        {
            return true;
        }

        return (group.First != null && ShouldStretch(group.First)) ||
               (group.Second != null && ShouldStretch(group.Second));
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to load application content")]
    private static partial void LogContentLoadingError(ILogger logger, Exception exception);

    private static UIElement GetApplicationContent(IDock dock, IViewLocator viewLocator, ILogger logger)
    {
        try
        {
            return TryGetApplicationContent(dock, viewLocator);
        }
        catch (Exception exception)
        {
            LogContentLoadingError(logger, exception);

            // Show the error as content
            return new TextBlock()
            {
                Text = exception.Message,
                TextWrapping = TextWrapping.Wrap,
            };
        }
    }

    private static UIElement TryGetApplicationContent(IDock dock, IViewLocator viewLocator)
    {
        if (dock.Dockables.Count != 1)
        {
            throw new ContentLoadingException(
                DebuggerConstants.AppOutletName,
                null,
                "the application dock must have exactly one dockable");
        }

        var contentViewModel = dock.Dockables[0].ViewModel;
        if (contentViewModel is null)
        {
            throw new ContentLoadingException(
                DebuggerConstants.AppOutletName,
                contentViewModel,
                "application view model is null");
        }

        var view = viewLocator.ResolveView(contentViewModel);
        if (view is UIElement content)
        {
            return content;
        }

        throw new ContentLoadingException(
            DebuggerConstants.AppOutletName,
            contentViewModel,
            $"the view is {(view is null ? "null" : "not a UIElement")}");
    }

    private void PushGrid(VectorGrid grid, string what)
    {
        Debug.WriteLine($"{(grid.Orientation == Orientation.Horizontal ? "---" : " | ")} Push grid {what}");
        this.grids.Push(grid);
    }

    private void PopGrid(string? what)
    {
        _ = this.grids.Pop();
        Debug.WriteLine($"{(this.grids.Peek().Orientation == Orientation.Horizontal ? "---" : " | ")} Pop grid {what}");
    }

    private void PlaceTray(IDockTray tray)
    {
        if (tray.IsEmpty)
        {
            return;
        }

        Debug.WriteLine($"Add tray {tray} to VG (fixed size)");
        var grid = this.grids.Peek();

        var trayOrientation = tray.IsVertical ? Orientation.Vertical : Orientation.Horizontal;
        var trayViewModel = new DockTrayViewModel(docker, tray, trayOrientation);
        var trayControl = new DockTray() { ViewModel = trayViewModel };
        grid.AddFixedSizeItem(trayControl, GridLength.Auto, 32);
    }

    private void Layout(IDockGroup group)
    {
        if (!ShouldShowGroup(group))
        {
            Debug.WriteLine($"Skipping {group}");
            return;
        }

        var stretch = ShouldStretch(group);

        var grid = this.grids.Peek();
        if (group.Orientation != DockGroupOrientation.Undetermined)
        {
            var orientation = group.Orientation == DockGroupOrientation.Vertical
                ? Orientation.Vertical
                : Orientation.Horizontal;
            if (grid.Orientation != orientation)
            {
                var newGrid = new VectorGrid(orientation);
                Debug.WriteLine($"Add new grid for group `{group} to VG");
                Debug.WriteLine($"Group {group.First} should {(stretch ? string.Empty : " NOT")}stretch");
                grid.AddResizableItem(
                    newGrid,
                    stretch ? new GridLength(1, GridUnitType.Star) : GridLength.Auto,
                    32);
                grid = newGrid;
            }
        }

        if (!group.IsEmpty)
        {
            // A group with docks -> Layout the docks as items in the vector grid.
            foreach (var dock in group.Docks.Where(d => d.State != DockingState.Minimized))
            {
                Debug.WriteLine($"Add dock {dock} from group `{group} to VG");
                grid.AddResizableItem(
                    new Border()
                    {
                        Child = dock is ApplicationDock
                            ? GetApplicationContent(dock, viewLocator, logger)
                            : new DockPanel()
                            {
                                ViewModel = new DockPanelViewModel(dock, docker),
                            },
                        BorderBrush = new SolidColorBrush(Colors.Red),
                        BorderThickness = new Thickness(0.5),
                    },
                    stretch ? new GridLength(1, GridUnitType.Star) : new GridLength(300),
                    32);
            }

            return;
        }

        // Handle the children
        this.PushGrid(grid, (grid == this.grids.Peek() ? "!!new " : "=same ") + group);
        HandlePart(group.First);
        HandlePart(group.Second);

        this.PopGrid(group.ToString());
        return;

        void HandlePart(IDockGroup? part)
        {
            switch (part)
            {
                case null:
                    return;

                case IDockTray tray:
                    this.PlaceTray(tray);
                    break;

                default:
                    this.Layout(part);
                    break;
            }
        }
    }
}
