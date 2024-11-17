// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;

namespace DroidNet.Routing.Debugger.UI.Docks;

/// <summary>
/// The ViewModel for the embedded application dock.
/// </summary>
/// <param name="appViewModel">The embedded application ViewModel.</param>
/// <param name="viewLocator">The view locator to be used to resolve the application view model into a view.</param>
/// <param name="logger">A logger to be used by this class.</param>
public partial class EmbeddedAppViewModel(object? appViewModel, IViewLocator viewLocator, ILogger logger)
{
    private UIElement? appView;

    /// <summary>
    /// Gets the application content as a UIElement. If the application view is not resolved yet,
    /// it will attempt to resolve it using the provided view locator.
    /// </summary>
    /// <value>The application content as a UIElement, or null if it cannot be resolved.</value>
    public UIElement? ApplicationContent
    {
        get
        {
            if (this.appView is null)
            {
                this.ResolveApplicationView();
            }

            return this.appView;
        }
    }

    /// <summary>
    /// Gets a description of the error that occurred during the resolution of the application view.
    /// </summary>
    /// <value>A string describing the error, or an empty string if no error occurred.</value>
    public string ErrorDescription { get; private set; } = string.Empty;

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to load application content because: {Because}")]
    private static partial void LogContentLoadingError(ILogger logger, string because);

    private void ResolveApplicationView()
    {
        if (appViewModel is null)
        {
            this.ErrorDescription
                = "the application dock must have exactly one dockable and its view model cannot be null";
            LogContentLoadingError(logger, this.ErrorDescription);
            return;
        }

        var resolved = viewLocator.ResolveView(appViewModel);
        if (resolved is UIElement view)
        {
            this.appView = view;
            return;
        }

        this.ErrorDescription
            = $"the resolved view for {appViewModel} is {(resolved is null ? "null" : "not a UIElement")}";
        LogContentLoadingError(logger, this.ErrorDescription);
    }
}
