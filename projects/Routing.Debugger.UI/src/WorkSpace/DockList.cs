// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using DroidNet.Docking;
using DroidNet.Routing.Debugger.UI.Docks;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;
using Application = Microsoft.UI.Xaml.Application;

/// <summary>
/// A custom control representing the list of docks in a dock group. It uses a
/// vector grid with a single row or column based on the group's orientation.
/// </summary>
public partial class DockList : VectorGrid
{
    public DockList(IDocker docker, IDockGroup group, ILogger logger)
        : base(
            group.Orientation == DockGroupOrientation.Horizontal
                ? Orientation.Horizontal
                : Orientation.Vertical)
    {
        this.Name = group.ToString();

        var isFirst = true;
        var index = 0;

        foreach (var dock in group.Docks.Where(d => d.State != DockingState.Minimized))
        {
            this.DefineItem(new GridLength(1, GridUnitType.Star));
            this.AddItem(
                new Border()
                {
                    Child = dock is ApplicationDock
                        ? GetApplicationContent(dock, logger)
                        : new DockPanel() { ViewModel = new DockPanelViewModel(dock, docker) },

                    // BorderBrush = new SolidColorBrush(Colors.Blue),
                    // BorderThickness = new Thickness(0.5),
                    // Margin = new Thickness(0),
                },
                index++);
            if (!isFirst)
            {
                this.DefineItem(GridLength.Auto);
                this.AddSplitter(index++);
            }

            isFirst = false;
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to load application content")]
    private static partial void LogContentLoadingError(ILogger logger, Exception exception);

    private static UIElement GetApplicationContent(IDock dock, ILogger logger)
    {
        try
        {
            return TryGetApplicationContent(dock);
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

    private static UIElement TryGetApplicationContent(IDock dock)
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

        // TODO: refactor this constant and provide a function to get the converter
        const string converterResourceKey = "VmToViewConverter";
        IValueConverter vmToViewConverter;
        try
        {
            vmToViewConverter = (IValueConverter)Application.Current.Resources[converterResourceKey] ??
                                throw new ContentLoadingException(
                                    DebuggerConstants.AppOutletName,
                                    contentViewModel,
                                    $"there is no ViewModel to View converter as a resource with key `{converterResourceKey}");
        }
        catch (Exception)
        {
            throw new ContentLoadingException(
                DebuggerConstants.AppOutletName,
                contentViewModel,
                $"there is no ViewModel to View converter as a resource with key `{converterResourceKey}`");
        }

        var view = vmToViewConverter.Convert(contentViewModel, typeof(object), null, null);
        if (view is UIElement content)
        {
            return content;
        }

        throw new ContentLoadingException(
            DebuggerConstants.AppOutletName,
            contentViewModel,
            $"the view for application content is {(view is null ? "null" : "not a UIElement")}");
    }
}
