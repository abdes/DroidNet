// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Diagnostics.CodeAnalysis;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.VisualStudio.TestTools.UnitTesting.AppContainer;

#if MSTEST_RUNNER
using Microsoft.Testing.Platform.Builder;
using Microsoft.VisualStudio.TestTools.UnitTesting;
#endif

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

/// <summary>
/// Provides application-specific behavior to supplement the default Application
/// class.
/// </summary>
[ExcludeFromCodeCoverage]
public partial class TestApp
{
    /// <summary>
    /// Initializes a new instance of the <see cref="TestApp" /> class.
    /// </summary>
    public TestApp() => this.InitializeComponent();

    /// <summary>
    /// Invoked when the application is launched normally by the end user.  Other entry
    /// points
    /// will be used such as when the application is launched to open a specific file.
    /// </summary>
    /// <param name="args">Details about the launch request and process.</param>
    protected override
#if MSTEST_RUNNER
        async
#endif
        void OnLaunched(LaunchActivatedEventArgs args)
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
