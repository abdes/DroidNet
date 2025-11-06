// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#if MSTEST_RUNNER
using Microsoft.Testing.Platform.Builder;
using Microsoft.VisualStudio.TestTools.UnitTesting;
#endif

using Microsoft.UI.Dispatching;
using Microsoft.VisualStudio.TestTools.UnitTesting.AppContainer;

namespace DroidNet.Samples.Tests;

/// <summary>
/// Provides application-specific behavior to supplement the default Application class.
/// </summary>
public partial class App
{
    /// <summary>
    /// Initializes a new instance of the <see cref="App" /> class. This is the first line of
    /// authored code executed, and as such is the logical equivalent of main() or WinMain().
    /// </summary>
    public App() => this.InitializeComponent();

    /// <summary>
    /// Invoked when the application is launched.
    /// </summary>
    /// <param name="args">Details about the launch request and process.</param>
    protected override
#if MSTEST_RUNNER
        async
#endif
        void OnLaunched(Microsoft.UI.Xaml.LaunchActivatedEventArgs args)
    {
#if !MSTEST_RUNNER
        Microsoft.VisualStudio.TestPlatform.TestExecutor.UnitTestClient.CreateDefaultUI();
#endif
        UITestMethodAttribute.DispatcherQueue = DispatcherQueue.GetForCurrentThread();

        // Replace back with e.Arguments when https://github.com/microsoft/microsoft-ui-xaml/issues/3368 is fixed
#if MSTEST_RUNNER
        // Ideally we would want to reuse the generated main so we don't have to manually handle all dependencies
        // but this type is generated too late in the build process so we fail before.
        // You can build, inspect the generated type to copy its content if you want.
        // await TestingPlatformEntryPoint.Main(Environment.GetCommandLineArgs().Skip(1).ToArray());
        var cliArgs = Environment.GetCommandLineArgs().Skip(1).ToArray();
        var builder = await TestApplication.CreateBuilderAsync(cliArgs).ConfigureAwait(false);
        Microsoft.Testing.Platform.MSBuild.TestingPlatformBuilderHook.AddExtensions(builder, cliArgs);
        Microsoft.Testing.Extensions.Telemetry.TestingPlatformBuilderHook.AddExtensions(builder, cliArgs);
        TestingPlatformBuilderHook.AddExtensions(builder, cliArgs);
        using var app = await builder.BuildAsync().ConfigureAwait(false);
        await app.RunAsync().ConfigureAwait(false);
#else
        Microsoft.VisualStudio.TestPlatform.TestExecutor.UnitTestClient.Run(Environment.CommandLine);
#endif
    }
}
