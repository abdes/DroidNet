// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Composition.Tests.TestSupport;
using DryIoc;
using static DroidNet.Composition.Tests.TestSupport.Fixtures;

namespace DroidNet.Composition.Tests;

/// <summary>
///     A scope handle disposes its child scopes, most recent first, then its owned async disposables,
///     most recent first, then its container; it never disposes a parent's instance.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Scope disposal")]
public sealed class ScopeDisposalTests
{
    private readonly ScopeFactory factory = new();

    [TestMethod]
    public async Task DisposeAsync_Scope_DisposesChildrenThenOwnedThenContainer()
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        await using var root = ScopeFactory.CreateRoot(host);
        var log = new EventLog();
        var session = this.CreateRecorded<ISessionLayer>(root, log, "session");
        _ = this.CreateRecorded<IWindowLayer>(session, log, "window 1");
        var window2 = this.CreateRecorded<IWindowLayer>(session, log, "window 2");
        _ = this.CreateRecorded<IPanelLayer>(window2, log, "panel");
        session.Own(new AsyncRecorder(log, "owned 1"));
        session.Own(new AsyncRecorder(log, "owned 2"));

        // Act
        await session.DisposeAsync().ConfigureAwait(true);

        // Assert
        _ = log.Events.Where(entry => entry.StartsWith("disposed", StringComparison.Ordinal)).Should().Equal(
            "disposed panel",
            "disposed window 2",
            "disposed window 1",
            "disposed owned 2",
            "disposed owned 1",
            "disposed session");
    }

    [TestMethod]
    public async Task DisposeAsync_Twice_DisposesOnce()
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        await using var root = ScopeFactory.CreateRoot(host);
        var log = new EventLog();
        var window = this.CreateRecorded<IWindowLayer>(root, log, "window");
        window.Own(new AsyncRecorder(log, "owned"));

        // Act
        await window.DisposeAsync().ConfigureAwait(true);
        await window.DisposeAsync().ConfigureAwait(true);

        // Assert
        _ = window.IsDisposed.Should().BeTrue();
        _ = log.Events.Should().Equal("created window", "disposed owned", "disposed window");
    }

    [TestMethod]
    public async Task DisposeAsync_ChildDisposedFirst_IsNotDisposedAgainByItsParent()
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        await using var root = ScopeFactory.CreateRoot(host);
        var log = new EventLog();
        var session = this.CreateRecorded<ISessionLayer>(root, log, "session");
        var window = this.CreateRecorded<IWindowLayer>(session, log, "window");
        await window.DisposeAsync().ConfigureAwait(true);

        // Act
        await session.DisposeAsync().ConfigureAwait(true);

        // Assert
        _ = log.Events.Should().Equal("created session", "created window", "disposed window", "disposed session");
    }

    [TestMethod]
    public async Task DisposeAsync_ChildScope_LeavesParentSingletonsAlive()
    {
        // Arrange
        using var host = TestScopes.CreateHost(root => root.Register<IService, Service>(Reuse.Singleton));
        await using var root = ScopeFactory.CreateRoot(host);
        var session = this.factory.CreateScope<ISessionLayer>(root, scope => scope.Register<Consumer>(Reuse.Singleton));
        var window = this.factory.CreateScope<IWindowLayer>(session);
        var service = (Service)window.Resolver.Resolve<IService>();
        _ = window.Resolver.Resolve<Consumer>();

        // Act
        await session.DisposeAsync().ConfigureAwait(true);

        // Assert
        _ = service.IsDisposed.Should().BeFalse();
        _ = host.Resolve<IService>().Should().BeSameAs(service);
    }

    [TestMethod]
    public async Task DisposeAsync_ScopeSingleton_IsDisposedWithTheScope()
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        await using var root = ScopeFactory.CreateRoot(host);
        var window = this.factory.CreateScope<IWindowLayer>(root, scope => scope.Register<IService, Service>(Reuse.Singleton));
        var service = (Service)window.Resolver.Resolve<IService>();

        // Act
        await window.DisposeAsync().ConfigureAwait(true);

        // Assert
        _ = service.IsDisposed.Should().BeTrue();
    }

    [TestMethod]
    [DataRow(false, DisplayName = "not tracked by default")]
    [DataRow(true, DisplayName = "tracked when it opts in")]
    public async Task DisposeAsync_DisposableTransientOfTheScope_IsDisposedOnlyWhenTracked(bool track)
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        await using var root = ScopeFactory.CreateRoot(host);
        var window = this.factory.CreateScope<IWindowLayer>(
            root,
            scope => scope.Register<IService, Service>(Reuse.Transient, setup: Setup.With(trackDisposableTransient: track)));
        var service = (Service)window.Resolver.Resolve<IService>();

        // Act
        await window.DisposeAsync().ConfigureAwait(true);

        // Assert
        _ = service.IsDisposed.Should().Be(track);
    }

    [TestMethod]
    public async Task DisposeAsync_Root_DisposesItsScopesButNotTheHostContainer()
    {
        // Arrange
        using var host = TestScopes.CreateHost(root => root.Register<IService, Service>(Reuse.Singleton));
        var root = ScopeFactory.CreateRoot(host);
        var log = new EventLog();
        _ = this.CreateRecorded<ISessionLayer>(root, log, "session");
        var service = (Service)host.Resolve<IService>();

        // Act
        await root.DisposeAsync().ConfigureAwait(true);

        // Assert
        _ = log.Events.Should().Equal("created session", "disposed session");
        _ = host.IsDisposed.Should().BeFalse();
        _ = service.IsDisposed.Should().BeFalse();
    }

    [TestMethod]
    public async Task Own_AfterDispose_Throws()
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        var root = ScopeFactory.CreateRoot(host);
        await root.DisposeAsync().ConfigureAwait(true);

        // Act
        var act = () => root.Own(new AsyncRecorder(new EventLog(), "late"));

        // Assert
        _ = act.Should().Throw<ObjectDisposedException>();
    }

    [TestMethod]
    public async Task Create_UnderDisposedParent_Throws()
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        var root = ScopeFactory.CreateRoot(host);
        await root.DisposeAsync().ConfigureAwait(true);

        // Act
        var act = () => this.factory.CreateScope<IWindowLayer>(root);

        // Assert
        _ = act.Should().Throw<ObjectDisposedException>();
    }

    private ScopeHandle CreateRecorded<TLayer>(ScopeHandle parent, EventLog log, string name)
        where TLayer : IScopeLayer
        => this.factory.Create<TLayer>(
            parent,
            [
                new TestModule<TLayer>(scope => scope.RegisterDelegate(_ => new NamedRecorder(log, name), Reuse.Singleton))
                {
                    Shadows = [typeof(NamedRecorder)],
                    Activation = [typeof(NamedRecorder)],
                },
            ]);
}
