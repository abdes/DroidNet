// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.VisualStudio.TestTools.UnitTesting.AppContainer;
using Windows.Graphics;

namespace DroidNet.Tests;

/// <summary>
/// Shared base Application class for all Visual User Interface tests.
/// Hosts the runner and creates a reusable window when tests need realized content.
/// </summary>
[ExcludeFromCodeCoverage]
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "The Application class must be public")]
public abstract class VisualUserInterfaceTestsApp : Application
{
    private Window? window;
    private DispatcherQueue? dispatcherQueue;

    /// <summary>
    /// Gets the reusable test window, creating it on first access from the UI thread.
    /// </summary>
    public static Window MainWindow => ((VisualUserInterfaceTestsApp)Current).GetOrCreateWindow();

    /// <summary>
    /// Gets or sets the content root of the main window.
    /// </summary>
    /// <remarks>
    /// In order to ensure that the content is fully realized, use the
    /// <see cref="VisualUserInterfaceTests.LoadTestContentAsync(FrameworkElement)"/> method.
    /// </remarks>
    public static FrameworkElement? ContentRoot
    {
        get => ((VisualUserInterfaceTestsApp)Current).window?.Content as FrameworkElement;
        set
        {
            // Fixture cleanup must not create a window for a dispatcher-only test.
            if (value is not null || ((VisualUserInterfaceTestsApp)Current).window is not null)
            {
                MainWindow.Content = value;
            }
        }
    }

    /// <summary>
    /// Gets the application's UI dispatcher without creating a test window.
    /// </summary>
    public static DispatcherQueue DispatcherQueue => ((VisualUserInterfaceTestsApp)Current).dispatcherQueue
        ?? throw new InvalidOperationException("The UI test application has not started.");

    /// <summary>
    /// Invoked when the application is launched.
    /// </summary>
    /// <param name="args">Details about the launch request and process.</param>
    protected override async void OnLaunched(LaunchActivatedEventArgs args)
    {
        this.dispatcherQueue = Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread()
            ?? throw new InvalidOperationException("The UI test application requires a dispatcher queue.");
        UITestMethodAttribute.DispatcherQueue = this.dispatcherQueue;

        Environment.ExitCode = 1;
        try
        {
            // Resume on the UI thread to close DroidNet's window after the run.
            Environment.ExitCode = await MSTestApplication.RunAsync(
                Environment.GetCommandLineArgs()[1..]).ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            // Application.Exit can end the message loop before async-void faults surface.
            Console.Error.WriteLine(exception);
            throw;
        }
        finally
        {
            this.window?.Close();
            this.Exit();
        }
    }

    private Window GetOrCreateWindow()
    {
        if (!DispatcherQueue.HasThreadAccess)
        {
            throw new InvalidOperationException("Access the test window on the UI thread.");
        }

        if (this.window is null)
        {
            this.window = new MainWindow();
            this.window.AppWindow.Resize(new SizeInt32(800, 600));
            this.window.Activate();
        }

        return this.window;
    }
}
