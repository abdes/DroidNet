// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

/// <summary>
/// A custom control to represent the entire docking tree.
/// </summary>
[ViewModel(typeof(WorkSpaceViewModel))]
public partial class WorkSpaceView : UserControl
{
    private readonly ILogger logger;

    /// <summary>
    /// Initializes a new instance of the <see cref="WorkSpaceView"/> class.
    /// </summary>
    /// <param name="loggerFactory">The factory to create logger instances.</param>
    public WorkSpaceView(ILoggerFactory? loggerFactory)
    {
        this.logger = loggerFactory?.CreateLogger("Workspace") ?? NullLoggerFactory.Instance.CreateLogger("Workspace");
        this.Style = (Style)Application.Current.Resources[nameof(WorkSpaceView)];

        this.ViewModelChanged += (_, args) =>
        {
            if (args.OldValue is not null)
            {
                args.OldValue.Layout.PropertyChanged -= this.ViewModelPropertyChanged;
            }

            this.UpdateContent();

            if (this.ViewModel is not null)
            {
                this.ViewModel.Layout.PropertyChanged += this.ViewModelPropertyChanged;
            }
        };

        this.Unloaded += (_, _) =>
        {
            if (this.ViewModel is not null)
            {
                this.ViewModel.Layout.PropertyChanged -= this.ViewModelPropertyChanged;
            }
        };
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Building workspace view from layout...")]
    private static partial void LogUpdatingLayout(ILogger logger);

    private void ViewModelPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (!string.Equals(args.PropertyName, nameof(this.ViewModel.Layout.Content), StringComparison.Ordinal))
        {
            return;
        }

        this.UpdateContent();
    }

    private void UpdateContent()
    {
        LogUpdatingLayout(this.logger);
        this.Content = this.ViewModel?.Layout.Content;
    }
}
