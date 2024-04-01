// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Docks;

using DroidNet.Hosting.Generators;
using DroidNet.Mvvm;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;

/// <summary>The ViewModel for the embedded application dock.</summary>
/// <param name="appViewModel">The embedded application ViewModel.</param>
/// <param name="viewLocator">The view locator to be used to resolve the application view model into a view.</param>
/// <param name="logger">A logger to be used by this class.</param>
[InjectAs(ServiceLifetime.Transient)]
public partial class EmbeddedAppViewModel(object? appViewModel, IViewLocator viewLocator, ILogger logger)
{
    private UIElement? appView;

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
