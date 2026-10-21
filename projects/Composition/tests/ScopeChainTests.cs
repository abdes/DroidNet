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
///     Resolution through the chain root &gt; session &gt; window &gt; panel: each scope resolves what it
///     does not own from its immediate parent, and the parent builds and caches it.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Scope chain")]
public sealed class ScopeChainTests
{
    private readonly ScopeFactory factory = new();

    [TestMethod]
    public async Task Resolve_RootSingletonFromEveryScope_ReturnsTheRootInstance()
    {
        // Arrange
        using var host = TestScopes.CreateHost(root => root.Register<IService, Service>(Reuse.Singleton));
        await using var chain = this.CreateChain(ScopeFactory.CreateRoot(host));

        // Act
        var fromPanel = chain.Panel.Resolver.Resolve<IService>();
        var fromWindow = chain.Window.Resolver.Resolve<IService>();
        var fromSession = chain.Session.Resolver.Resolve<IService>();
        var fromRoot = host.Resolve<IService>();

        // Assert: the panel resolved first, and the root still owns the one instance.
        _ = fromPanel.Should().BeSameAs(fromRoot);
        _ = fromWindow.Should().BeSameAs(fromRoot);
        _ = fromSession.Should().BeSameAs(fromRoot);
    }

    [TestMethod]
    public async Task Resolve_SessionServiceFromWindowAndPanel_ReturnsTheSessionInstance()
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        await using var root = ScopeFactory.CreateRoot(host);
        var session = this.factory.CreateScope<ISessionLayer>(root, scope => scope.Register<IService, Service>(Reuse.Singleton));
        var window = this.factory.CreateScope<IWindowLayer>(session);
        var panel = this.factory.CreateScope<IPanelLayer>(window);

        // Act
        var fromPanel = panel.Resolver.Resolve<IService>();

        // Assert
        _ = fromPanel.Should().BeSameAs(window.Resolver.Resolve<IService>());
        _ = fromPanel.Should().BeSameAs(session.Resolver.Resolve<IService>());
        _ = host.Resolve<IService>(IfUnresolved.ReturnDefault).Should().BeNull("the root does not see a session service");
    }

    [TestMethod]
    public async Task Resolve_KeyedRootService_ReturnsTheInstanceOfThatKey()
    {
        // Arrange
        using var host = TestScopes.CreateHost(root =>
        {
            root.Register<IService, Service>(Reuse.Singleton, serviceKey: "first");
            root.Register<IService, OtherService>(Reuse.Singleton, serviceKey: "second");
        });
        await using var chain = this.CreateChain(ScopeFactory.CreateRoot(host));

        // Act
        var first = chain.Panel.Resolver.Resolve<IService>("first");
        var second = chain.Panel.Resolver.Resolve<IService>("second");

        // Assert
        _ = first.Should().BeSameAs(host.Resolve<IService>("first"));
        _ = second.Should().BeOfType<OtherService>().And.BeSameAs(host.Resolve<IService>("second"));
    }

    [TestMethod]
    public async Task Resolve_ConstructedTypeOfRootOpenGeneric_ReturnsTheRootInstance()
    {
        // Arrange
        using var host = TestScopes.CreateHost(root => root.Register(typeof(IGenericService<>), typeof(GenericService<>), Reuse.Singleton));
        await using var chain = this.CreateChain(ScopeFactory.CreateRoot(host));

        // Act
        var fromPanel = chain.Panel.Resolver.Resolve<IGenericService<int>>();

        // Assert
        _ = fromPanel.Should().BeSameAs(host.Resolve<IGenericService<int>>());
    }

    [TestMethod]
    public async Task Resolve_FuncAndLazyOfRootService_ReturnTheRootInstance()
    {
        // Arrange
        using var host = TestScopes.CreateHost(root => root.Register<IService, Service>(Reuse.Singleton));
        await using var chain = this.CreateChain(ScopeFactory.CreateRoot(host));

        // Act
        var func = chain.Panel.Resolver.Resolve<Func<IService>>();
        var lazy = chain.Panel.Resolver.Resolve<Lazy<IService>>();

        // Assert
        _ = func().Should().BeSameAs(host.Resolve<IService>());
        _ = lazy.Value.Should().BeSameAs(host.Resolve<IService>());
    }

    [TestMethod]
    public async Task Resolve_ScopeServiceDependingOnRootService_ReceivesTheRootInstance()
    {
        // Arrange
        using var host = TestScopes.CreateHost(root => root.Register<IService, Service>(Reuse.Singleton));
        await using var root = ScopeFactory.CreateRoot(host);
        var window = this.factory.CreateScope<IWindowLayer>(root, scope => scope.Register<Consumer>(Reuse.Singleton));

        // Act
        var consumer = window.Resolver.Resolve<Consumer>();

        // Assert
        _ = consumer.Service.Should().BeSameAs(host.Resolve<IService>());
    }

    [TestMethod]
    public async Task Resolve_RootTransient_IsBuiltWithTheRootDependencies()
    {
        // Arrange: the window shadows the consumer's dependency, but the consumer is the root's.
        using var host = TestScopes.CreateHost(root =>
        {
            root.Register<IService, Service>(Reuse.Singleton);
            root.Register<Consumer>(Reuse.Transient);
        });
        await using var root = ScopeFactory.CreateRoot(host);
        var window = this.factory.Create<IWindowLayer>(
            root,
            [new TestModule<IWindowLayer>(scope => scope.Register<IService, OtherService>(Reuse.Singleton)) { Shadows = [typeof(IService)] }]);

        // Act
        var consumer = window.Resolver.Resolve<Consumer>();

        // Assert
        _ = window.Resolver.Resolve<IService>().Should().BeOfType<OtherService>();
        _ = consumer.Service.Should().BeSameAs(host.Resolve<IService>());
    }

    [TestMethod]
    public async Task Resolve_OptionalDependencyNoScopeRegisters_ReceivesTheDefault()
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        await using var chain = this.CreateChain(ScopeFactory.CreateRoot(host));
        var panel = this.factory.CreateScope<IPanelLayer>(chain.Window, scope => scope.Register<OptionalConsumer>(Reuse.Transient));

        // Act
        var consumer = panel.Resolver.Resolve<OptionalConsumer>();

        // Assert
        _ = consumer.Missing.Should().BeNull();
    }

    [TestMethod]
    public async Task Resolve_ReturnDefaultForServiceNoScopeRegisters_ReturnsNull()
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        await using var chain = this.CreateChain(ScopeFactory.CreateRoot(host));

        // Act
        var missing = chain.Panel.Resolver.Resolve<IMissingService>(IfUnresolved.ReturnDefault);

        // Assert
        _ = missing.Should().BeNull();
    }

    [TestMethod]
    public async Task Resolve_ServiceNoScopeRegisters_Throws()
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        await using var chain = this.CreateChain(ScopeFactory.CreateRoot(host));

        // Act
        var act = () => chain.Panel.Resolver.Resolve<IMissingService>();

        // Assert
        _ = act.Should().Throw<ContainerException>();
    }

    [TestMethod]
    public async Task Resolve_AfterTheParentRegistersTheService_ReturnsTheParentInstance()
    {
        // Arrange: the window asks before the root registers the service.
        using var host = TestScopes.CreateHost();
        await using var root = ScopeFactory.CreateRoot(host);
        var window = this.factory.CreateScope<IWindowLayer>(root);
        _ = window.Resolver.Resolve<IService>(IfUnresolved.ReturnDefault);

        // Act
        host.Register<IService, Service>(Reuse.Singleton);
        var service = window.Resolver.Resolve<IService>();

        // Assert
        _ = service.Should().BeSameAs(host.Resolve<IService>());
    }

    [TestMethod]
    public async Task Resolve_RootDefaultLeftAfterAnotherIsUnregistered_ReturnsTheRemainingDefault()
    {
        // Arrange: the remaining default registration keeps its default key, DefaultKey.Of(1).
        using var host = TestScopes.CreateHost(root =>
        {
            root.Register<IService, Service>(Reuse.Singleton);
            root.Register<IService, OtherService>(Reuse.Singleton);
        });
        host.Unregister<IService>(serviceKey: DefaultKey.Value);
        await using var root = ScopeFactory.CreateRoot(host);
        var window = this.factory.CreateScope<IWindowLayer>(root);

        // Act
        var service = window.Resolver.Resolve<IService>();

        // Assert
        _ = service.Should().BeOfType<OtherService>().And.BeSameAs(host.Resolve<IService>());
    }

    [TestMethod]
    public async Task Resolve_CollectionOfRootServices_SeesOnlyTheScopeRegistrations()
    {
        // Arrange
        using var host = TestScopes.CreateHost(root => root.Register<IService, Service>(Reuse.Singleton));
        await using var root = ScopeFactory.CreateRoot(host);
        var window = this.factory.CreateScope<IWindowLayer>(root);

        // Act
        var services = window.Resolver.Resolve<IEnumerable<IService>>();

        // Assert
        _ = services.Should().BeEmpty();
    }

    [TestMethod]
    public async Task Resolve_ServiceProvider_IsTheScopeItself()
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        await using var root = ScopeFactory.CreateRoot(host);
        var window = this.factory.CreateScope<IWindowLayer>(root, scope => scope.Register<IService, Service>(Reuse.Singleton));

        // Act
        var provider = window.Resolver.Resolve<IServiceProvider>();

        // Assert
        _ = provider.GetService(typeof(IService)).Should().BeSameAs(window.Resolver.Resolve<IService>());
    }

    [TestMethod]
    public async Task Create_ModuleOfEveryLayer_IsAcceptedAtAnyLayer()
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        await using var root = ScopeFactory.CreateRoot(host);
        IScopeModule<IScopeLayer> everyLayer = new TestModule<IScopeLayer>(scope => scope.Register<IService, Service>(Reuse.Singleton));

        // Act
        var session = this.factory.Create<ISessionLayer>(root, [everyLayer]);
        var window = this.factory.Create<IWindowLayer>(root, [everyLayer, new TestModule<IWindowLayer>(_ => { })]);

        // Assert
        _ = session.Resolver.Resolve<IService>().Should().NotBeSameAs(window.Resolver.Resolve<IService>());
    }

    [TestMethod]
    public async Task Resolve_ScopeHandleInEveryScope_ReturnsThatScopesOwnHandle()
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        await using var chain = this.CreateChain(ScopeFactory.CreateRoot(host));

        // Act
        var fromRoot = host.Resolve<ScopeHandle>();
        var fromSession = chain.Session.Resolver.Resolve<ScopeHandle>();
        var fromWindow = chain.Window.Resolver.Resolve<ScopeHandle>();
        var fromPanel = chain.Panel.Resolver.Resolve<ScopeHandle>();

        // Assert
        _ = fromRoot.Should().BeSameAs(chain.Root);
        _ = fromSession.Should().BeSameAs(chain.Session);
        _ = fromWindow.Should().BeSameAs(chain.Window);
        _ = fromPanel.Should().BeSameAs(chain.Panel);
    }

    [TestMethod]
    public async Task Resolve_ScopeOwner_ReceivesTheHandleOfTheScopeThatBuildsIt()
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        await using var root = ScopeFactory.CreateRoot(host);
        var window = this.factory.CreateScope<IWindowLayer>(root, scope => scope.Register<ScopeOwner>(Reuse.Transient));
        var panel = this.factory.CreateScope<IPanelLayer>(window);

        // Act
        var owner = panel.Resolver.Resolve<ScopeOwner>();

        // Assert: the window builds the owner, so the owner's scopes go under the window.
        _ = owner.Scope.Should().BeSameAs(window);
    }

    [TestMethod]
    public void CreateRoot_HostWithARootScope_Throws()
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        _ = ScopeFactory.CreateRoot(host);

        // Act
        var act = () => ScopeFactory.CreateRoot(host);

        // Assert
        _ = act.Should().Throw<ContainerException>();
    }

    private Chain CreateChain(ScopeHandle root)
    {
        var session = this.factory.CreateScope<ISessionLayer>(root);
        var window = this.factory.CreateScope<IWindowLayer>(session);
        var panel = this.factory.CreateScope<IPanelLayer>(window);
        return new Chain(root, session, window, panel);
    }

    private sealed record Chain(ScopeHandle Root, ScopeHandle Session, ScopeHandle Window, ScopeHandle Panel) : IAsyncDisposable
    {
        public ValueTask DisposeAsync() => this.Root.DisposeAsync();
    }
}
