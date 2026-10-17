// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using AwesomeAssertions;
using DroidNet.Config;
using DroidNet.Hosting.WinUI;
using Moq;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Runtime.Tests;

/// <summary>Checks that the engine starts with its startup settings and takes logging verbosity live.</summary>
[TestClass]
public sealed class EngineStartupSettingsTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task EngineStartsWithTheStartupLoggingVerbosity()
    {
        var settings = Settings(verbosity: 2);
        var native = new FakeEngineSession();
        var service = Create(native, settings);
        await using var lifetime = service.ConfigureAwait(false);

        _ = await service.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = native.StartupSettings.Should().BeSameAs(settings.Object.Settings);
        _ = service.EngineLoggingVerbosity.Should().Be(2);
    }

    [TestMethod]
    public async Task VerbosityChangeReachesTheRunningEngine()
    {
        var settings = Settings(verbosity: EngineConstants.DefaultLoggingVerbosity);
        var native = new FakeEngineSession();
        var service = Create(native, settings);
        await using var lifetime = service.ConfigureAwait(false);
        _ = await service.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await service.StartAsync().ConfigureAwait(false);

        settings.Object.Settings.LoggingVerbosity = 3;
        settings.Raise(value => value.PropertyChanged += null, new PropertyChangedEventArgs(nameof(IEngineStartupSettings.LoggingVerbosity)));

        _ = native.LoggingVerbosity.Should().Be(3);
    }

    [TestMethod]
    public async Task VerbosityChangeWithoutAnEngineWaitsForTheNextStart()
    {
        var settings = Settings(verbosity: EngineConstants.DefaultLoggingVerbosity);
        var native = new FakeEngineSession();
        var service = Create(native, settings);
        await using var lifetime = service.ConfigureAwait(false);

        settings.Object.Settings.LoggingVerbosity = 4;
        settings.Raise(value => value.PropertyChanged += null, new PropertyChangedEventArgs(nameof(IEngineStartupSettings.LoggingVerbosity)));
        _ = await service.InitializeAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = native.LoggingVerbosity.Should().Be(4);
    }

    private static Mock<ISettingsService<IEngineStartupSettings>> Settings(int verbosity)
    {
        var settings = new Mock<ISettingsService<IEngineStartupSettings>>();
        _ = settings.SetupGet(value => value.Settings).Returns(new EngineStartupSettings { LoggingVerbosity = verbosity });
        return settings;
    }

    private static EngineService Create(FakeEngineSession native, Mock<ISettingsService<IEngineStartupSettings>> settings)
        => new(
            new HostingContext { Dispatcher = null!, Application = null!, DispatcherScheduler = null! },
            () => native,
            Mock.Of<IOperationResultPublisher>(),
            loggerFactory: null,
            engineSettings: settings.Object);
}
