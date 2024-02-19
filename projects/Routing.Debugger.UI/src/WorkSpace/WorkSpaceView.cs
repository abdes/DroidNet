// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using System.Reactive.Linq;
using DroidNet.Routing.Generators;
using DroidNet.Routing.View;
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
    public WorkSpaceView(ILoggerFactory? loggerFactory, IViewLocator viewLocator)
    {
        this.Style = (Style)Application.Current.Resources[nameof(WorkSpaceView)];

        var logger = loggerFactory?.CreateLogger("Workspace") ?? NullLoggerFactory.Instance.CreateLogger("Workspace");

        this.ViewModelChanged += (_, _)
            =>
        {
            var layout = new WorkSpaceLayout(this.ViewModel.Docker, viewLocator, logger);
            this.Content = layout.UpdateContent();

            _ = Observable.FromEvent(
                    h => this.ViewModel.Docker.LayoutChanged += h,
                    h => this.ViewModel.Docker.LayoutChanged -= h)
                .Throttle(TimeSpan.FromMilliseconds(200))
                .ObserveOn(System.Windows.Threading.Dispatcher.CurrentDispatcher)
                .Subscribe(_ => this.Content = layout.UpdateContent());
        };

        this.Unloaded += (_, _) => this.ViewModel.Dispose();
    }
}
