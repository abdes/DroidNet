// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using System.Diagnostics;
using DroidNet.Docking;
using DroidNet.Routing.Generators;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

/// <summary>
/// A custom control to represent the entire docking tree. Each docking group is
/// represented as a two-item grid, in a single row or column, based on the
/// group's orientation.
/// </summary>
[ViewModel(typeof(WorkSpaceViewModel))]
public partial class WorkSpaceView : UserControl
{
    private readonly ILogger logger;

    public WorkSpaceView(ILoggerFactory? loggerFactory)
    {
        this.logger = loggerFactory?.CreateLogger("Workspace") ?? NullLoggerFactory.Instance.CreateLogger("Workspace");

        this.Style = (Style)Application.Current.Resources[nameof(WorkSpaceView)];

        this.ViewModelChanged += (_, _) =>
        {
            this.UpdateContent();
            this.ViewModel.Docker.LayoutChanged += this.UpdateContent;
        };

        this.Unloaded += (_, _) => this.ViewModel.Dispose();
    }

    private static UIElement? ContentForDockGroup(IDocker docker, IDockGroup group, ILogger logger)
    {
        if (!group.IsEmpty)
        {
            return GroupWithDocks(docker, group, logger);
        }

        var content = GroupWithNoDocks(docker, group, logger);
        return content == null
            ? null
            : new Border()
            {
                Child = content,

                // BorderBrush = new SolidColorBrush(Colors.Red),
                // BorderThickness = new Thickness(0.5),
            };
    }

    private static DockGroup? GroupWithNoDocks(IDocker docker, IDockGroup group, ILogger logger)
    {
        var showFirst = ShouldShowGroup(group.First);
        var showSecond = ShouldShowGroup(group.Second);

        return showFirst || showSecond ? new DockGroup(docker, group, showFirst, showSecond, logger) : null;
    }

    private static bool ShouldShowGroup(IDockGroup? group)
    {
        if (group is null)
        {
            return false;
        }

        if (group is IDockTray { IsEmpty: false })
        {
            return true;
        }

        if (!group.IsEmpty)
        {
            return group.Docks.Any(d => d.State != DockingState.Minimized);
        }

        return ShouldShowGroup(group.First) || ShouldShowGroup(group.Second);
    }

    private static DockList? GroupWithDocks(IDocker docker, IDockGroup group, ILogger logger)
        => group.Docks.Any(d => d.State != DockingState.Minimized) ? new DockList(docker, group, logger) : null;

    private void UpdateContent() => this.Content = new Border()
    {
        Child = ContentForDockGroup(this.ViewModel.Docker, this.ViewModel.Root, this.logger),
        BorderBrush = this.BorderBrush,
        BorderThickness = this.BorderThickness,
    };

    private sealed class DockGroup : VectorGrid
    {
        private readonly IDocker docker;

        public DockGroup(IDocker docker, IDockGroup group, bool showFirst, bool showSecond, ILogger logger)
            : base(group.IsHorizontal ? Orientation.Horizontal : Orientation.Vertical)
        {
            this.docker = docker;
            this.Name = group.ToString();
            this.BuildGrid(group, showFirst, showSecond, logger);
        }

        private void AddContentForSlot(VectorGrid grid, IDockGroup slot, int index, ILogger logger)
        {
            var content = ContentForDockGroup(this.docker, slot, logger);
            if (content != null)
            {
                grid.AddItem(content, index);
            }
        }

        private void BuildGrid(IDockGroup group, bool showFirst, bool showSecond, ILogger logger)
        {
            Debug.Assert(
                showFirst || showSecond,
                "do not build a grid unless at least one part of the group is to be shown");

            var gridItemIndex = 0;
            var showSplitter = false;

            if (showFirst)
            {
                Debug.Assert(group.First is not null, "if you request to show the first part, it should not be null");

                if (!HandleTray(group.First) && !Collapse(group.First, group.First.First, group.First.Second))
                {
                    this.DefineItem(new GridLength(1, GridUnitType.Star));
                    this.AddContentForSlot(this, group.First!, gridItemIndex++, logger);
                    showSplitter = true;
                }
            }

            if (showSecond)
            {
                Debug.Assert(group.Second is not null, "if you request to show the second part, it should not be null");

                if (!HandleTray(group.Second) && !Collapse(group.Second, group.Second.Second, group.Second.First))
                {
                    if (showSplitter)
                    {
                        this.DefineItem(GridLength.Auto);
                        this.AddSplitter(gridItemIndex++);
                    }

                    this.DefineItem(new GridLength(1, GridUnitType.Star));
                    this.AddContentForSlot(this, group.Second!, gridItemIndex, logger);
                }
            }

            return;

            bool HandleTray(IDockGroup part)
            {
                if (part is not IDockTray tray)
                {
                    return false;
                }

                this.DefineItem(GridLength.Auto);
                var trayOrientation = part.IsVertical ? Orientation.Vertical : Orientation.Horizontal;
                var trayControl = new DockTray(tray.MinimizedDocks, trayOrientation);
                this.AddItem(trayControl, gridItemIndex++);
                return true;
            }

            bool Collapse(IDockGroup part, IDockGroup? tray, IDockGroup? other)
            {
                // Collapse the group if it has a tray as part and the other
                // part is not to be shown.
                if (tray is not IDockTray || ShouldShowGroup(other))
                {
                    return false;
                }

                this.DefineItem(GridLength.Auto);
                this.AddContentForSlot(this, part, gridItemIndex++, logger);
                return true;
            }
        }
    }
}
