// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Reactive.Concurrency;
using System.Reactive.Subjects;
using DroidNet.Hosting.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.TestSupport;

/// <summary>
///     Owns the one native engine a test process shares, as the editor shares one engine across
///     projects and documents. Fixtures open their documents on it and return it to a running,
///     surface-free baseline; only a test that needs different startup settings, or that tests
///     shutdown itself, stops it, and the next fixture starts it again.
/// </summary>
/// <remarks>All members must be called on the UI dispatcher; the test assembly does not parallelize.</remarks>
internal static class SharedNativeEngine
{
    private static readonly RoutedResults Results = new();
    private static Oxygen.Testing.TemporaryNativeArtifacts? compatibility;
    private static EngineService? engine;

    /// <summary>Gets the shared engine service, creating it (not yet started) on first use.</summary>
    internal static EngineService Engine
    {
        get
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
                var settings = new Mock<DroidNet.Config.ISettingsService<IEngineSettings>>();
                _ = settings.SetupGet(value => value.Settings).Returns(new EngineSettings());
                compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
                engine = new EngineService(hosting, Results, loggerFactory: null, engineSettings: settings.Object, nativeCompatibility: compatibility);
            }

            return engine;
        }
    }

    /// <summary>Routes engine diagnostics to a fixture's result queue until the returned scope is disposed.</summary>
    /// <param name="results">The queue that receives engine results.</param>
    /// <returns>A scope that restores the previous receiver.</returns>
    internal static IDisposable RouteResults(ConcurrentQueue<OperationResult> results) => Results.Push(results);

    /// <summary>Starts the shared engine if it is not running.</summary>
    /// <param name="cancellationToken">Cancels initialization.</param>
    /// <returns><see langword="true"/> when the engine is running.</returns>
    internal static async Task<bool> EnsureRunningAsync(CancellationToken cancellationToken)
    {
        var service = Engine;
        if (!await service.InitializeAsync(cancellationToken).ConfigureAwait(true))
        {
            return false;
        }

        service.TargetFps = 60;
        await service.StartAsync().ConfigureAwait(true);
        return true;
    }

    /// <summary>Stops the shared engine so a test can own the native runtime; the next fixture restarts it.</summary>
    /// <returns>The shutdown task.</returns>
    internal static async Task StandDownAsync()
    {
        if (engine is not null)
        {
            await engine.ShutdownAsync().ConfigureAwait(true);
        }
    }

    /// <summary>
    ///     Returns the shared engine to its baseline after the last open fixture closes its document:
    ///     no project content mounted and the default frame rate. A fixture that leaked surfaces, or
    ///     left the engine faulted, costs a restart so the next test does not inherit its state.
    /// </summary>
    /// <returns>The reset task.</returns>
    internal static async Task ResetAsync()
    {
        if (engine is null || Results.HasReceivers)
        {
            return;
        }

        if (engine.State is not EngineServiceState.Running || engine.ActiveSurfaceCount != 0)
        {
            await engine.ShutdownAsync().ConfigureAwait(true);
            return;
        }

        try
        {
            await engine.RefreshProjectCookedRootsAsync([]).ConfigureAwait(true);
            engine.TargetFps = 60;
        }
        catch (InvalidOperationException)
        {
            await engine.ShutdownAsync().ConfigureAwait(true);
        }
    }

    /// <summary>Disposes the shared engine at the end of the test process.</summary>
    /// <returns>The disposal task.</returns>
    internal static async Task DisposeAsync()
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
    }

    /// <summary>Forwards engine results to the innermost active fixture.</summary>
    private sealed class RoutedResults : IOperationResultPublisher
    {
        private readonly Subject<OperationResult> published = new();
        private readonly Stack<ConcurrentQueue<OperationResult>> receivers = new();

        public bool HasReceivers
        {
            get
            {
                lock (this.receivers)
                {
                    return this.receivers.Count != 0;
                }
            }
        }

        public IDisposable Push(ConcurrentQueue<OperationResult> receiver)
        {
            lock (this.receivers)
            {
                this.receivers.Push(receiver);
            }

            return new Scope(this, receiver);
        }

        public void Publish(OperationResult result)
        {
            lock (this.receivers)
            {
                if (this.receivers.TryPeek(out var receiver))
                {
                    receiver.Enqueue(result);
                }
            }

            this.published.OnNext(result);
        }

        public IDisposable Subscribe(IObserver<OperationResult> observer) => this.published.Subscribe(observer);

        private void Pop(ConcurrentQueue<OperationResult> receiver)
        {
            lock (this.receivers)
            {
                // Fixtures nest (a producer project inside a consumer test), so scopes close innermost first;
                // tolerate out-of-order disposal by removing the receiver wherever it sits.
                var remaining = this.receivers.Where(value => !ReferenceEquals(value, receiver)).Reverse().ToArray();
                this.receivers.Clear();
                foreach (var value in remaining)
                {
                    this.receivers.Push(value);
                }
            }
        }

        private sealed class Scope(RoutedResults owner, ConcurrentQueue<OperationResult> receiver) : IDisposable
        {
            private bool disposed;

            public void Dispose()
            {
                if (!this.disposed)
                {
                    this.disposed = true;
                    owner.Pop(receiver);
                }
            }
        }
    }
}
