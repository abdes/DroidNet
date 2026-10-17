// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Concurrency;
using CommunityToolkit.WinUI;
using DroidNet.Config;
using DroidNet.Hosting.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Tests.TestSupport.Native;

/// <summary>
/// The one real engine of the integration suite: it starts on first use and stays running for every test, which
/// returns it to its baseline (no content mounted) when it ends.
/// </summary>
/// <remarks>All members are called on the UI dispatcher; the suite does not parallelize.</remarks>
[TestClass]
public static class SharedEngine
{
    private static readonly List<OperationResult> PublishedResults = [];
    private static Oxygen.Testing.TemporaryNativeArtifacts? compatibility;
    private static EngineService? engine;

    /// <summary>Gets the engine diagnostics published since the last reset.</summary>
    public static IReadOnlyList<OperationResult> Results => PublishedResults;

    /// <summary>Starts the shared engine if it does not run.</summary>
    /// <param name="cancellationToken">Cancels the start.</param>
    /// <returns>The running engine.</returns>
    public static async Task<EngineService> EnsureRunningAsync(CancellationToken cancellationToken)
    {
        if (engine is null)
        {
            var dispatcher = VisualUserInterfaceTestsApp.DispatcherQueue;
            var hosting = new HostingContext
            {
                Application = Application.Current,
                Dispatcher = dispatcher,
                DispatcherScheduler = new DispatcherQueueScheduler(dispatcher),
                IsRunning = true,
            };
            var settings = new Mock<ISettingsService<IEngineStartupSettings>>();
            _ = settings.SetupGet(value => value.Settings).Returns(new EngineStartupSettings());
            var results = new Mock<IOperationResultPublisher>();
            _ = results.Setup(value => value.Publish(It.IsAny<OperationResult>())).Callback<OperationResult>(PublishedResults.Add);
            compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
            engine = new EngineService(hosting, results.Object, loggerFactory: null, engineSettings: settings.Object, nativeCompatibility: compatibility);
        }

        if (engine.State != EngineServiceState.Running)
        {
            if (!await engine.InitializeAsync(cancellationToken).ConfigureAwait(true))
            {
                throw new InvalidOperationException("The shared engine did not initialize.");
            }

            await engine.StartAsync().ConfigureAwait(true);
        }

        return engine;
    }

    /// <summary>Returns the engine to its baseline: no project content mounted.</summary>
    /// <returns>The reset task.</returns>
    public static async Task ResetAsync()
    {
        PublishedResults.Clear();
        if (engine is { State: EngineServiceState.Running } running)
        {
            await running.RefreshProjectCookedRootsAsync([]).ConfigureAwait(true);
        }
    }

    /// <summary>Disposes the shared engine after the last test, on the UI dispatcher.</summary>
    /// <param name="context">The test run, which cancels the cleanup when the run is cancelled.</param>
    /// <returns>The cleanup task.</returns>
    [AssemblyCleanup]
    public static Task DisposeAsync(TestContext context)
        => VisualUserInterfaceTestsApp.DispatcherQueue.EnqueueAsync(async () =>
        {
            try
            {
                if (engine is not null)
                {
                    await engine.DisposeAsync().ConfigureAwait(true);
                }
            }
            finally
            {
                engine = null;
                compatibility?.Dispose();
                compatibility = null;
            }
        }).WaitAsync(TimeSpan.FromSeconds(60), context.CancellationToken);
}
