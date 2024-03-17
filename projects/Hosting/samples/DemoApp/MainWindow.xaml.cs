// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

/* ReSharper disable PrivateFieldCanBeConvertedToLocalVariable */

namespace DroidNet.Hosting.Demo;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Hosting.Demo.Services;
using DryIoc;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;

/// <summary>The User Interface's main window.</summary>
[ExcludeFromCodeCoverage]
public sealed partial class MainWindow
{
    private const string BooleanFlagKey = "boolean-flag";
    private const string IntValueKey = "int-value";
    private const string StringValueKey = "string-value";

    private const string SettingsSectionName = ExampleSettings.Section;

    private readonly bool booleanFlag;
    private readonly int intValue;

    private readonly IHostApplicationLifetime lifetime;
    private readonly ILogger logger;
    private readonly ExampleSettings? settings;
    private readonly string? stringValue;
    private readonly IContainer container;

    /// <summary>Initializes a new instance of the <see cref="MainWindow" /> class.</summary>
    /// <remarks>
    /// This window is created and activated when the <see cref="lifetime" /> is Launched. This is preferred to the alternative of
    /// doing that in the hosted service to keep the control of window creation and destruction under the application itself. Not
    /// all applications have a single window, and it is often not obvious which window is considered the main window, which is
    /// important in determining when the UI lifetime ends.
    /// </remarks>
    /// <param name="container">The dependency injection container.</param>
    /// <param name="lifetime">The hosted application lifetime. Used to exit the application programmatically.</param>
    /// <param name="config">Configuration settings, injected by the Dependency Injector.</param>
    /// <param name="logger">The logger instance to be used by this class.</param>
    public MainWindow(
        IContainer container,
        IHostApplicationLifetime lifetime,
        IConfiguration config,
        ILogger<MainWindow> logger)
    {
        this.container = container;
        this.lifetime = lifetime;
        this.logger = logger;

        this.booleanFlag = config.GetValue<bool>(BooleanFlagKey);
        this.intValue = config.GetValue<int>(IntValueKey);
        this.stringValue = config.GetValue<string>(StringValueKey);

        this.settings = config.GetSection(SettingsSectionName).Get<ExampleSettings>();

        this.InitializeComponent();
    }

    private void Exit(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        this.lifetime.StopApplication();
    }

    private void LogSomething(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        using var scopedContainer = this.container.OpenScope();

        // Resolve with a wrapper that passes additional non-injected data to the service.
        var test = scopedContainer.Resolve<Func<string, ITestInterface>>()(nameof(MainWindow));

        this.Something(test.Message);
    }

    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Warning, Message = "{Message}")]
    partial void Something(string message);
}
