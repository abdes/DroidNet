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
///     A scope that registers a service of its parent chain declares it in its modules' shadows; the
///     factory refuses an undeclared shadow, by type and key, including generic types.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Scope shadows")]
public sealed class ScopeShadowTests
{
    private readonly ScopeFactory factory = new();

    [TestMethod]
    public async Task Create_DeclaredShadow_ResolvesTheScopeInstance()
    {
        // Arrange
        using var host = TestScopes.CreateHost(root => root.Register<IService, Service>(Reuse.Singleton));
        await using var root = ScopeFactory.CreateRoot(host);

        // Act
        var window = this.CreateWindow(root, scope => scope.Register<IService, OtherService>(Reuse.Singleton), typeof(IService));

        // Assert
        _ = window.Resolver.Resolve<IService>().Should().BeOfType<OtherService>();
        _ = host.Resolve<IService>().Should().BeOfType<Service>();
    }

    [TestMethod]
    public async Task Create_UndeclaredShadowOfRootService_ThrowsWithTheRegistration()
    {
        // Arrange
        using var host = TestScopes.CreateHost(root => root.Register<IService, Service>(Reuse.Singleton));
        await using var root = ScopeFactory.CreateRoot(host);

        // Act
        var act = () => this.CreateWindow(root, scope => scope.Register<IService, OtherService>(Reuse.Singleton));

        // Assert
        _ = act.Should().Throw<UndeclaredShadowException>()
            .Which.Registrations.Should().ContainSingle()
            .Which.ServiceType.Should().Be<IService>();
    }

    [TestMethod]
    public async Task Create_UndeclaredShadowOfGrandparentService_Throws()
    {
        // Arrange
        using var host = TestScopes.CreateHost(root => root.Register<IService, Service>(Reuse.Singleton));
        await using var root = ScopeFactory.CreateRoot(host);
        var session = this.factory.CreateScope<ISessionLayer>(root);

        // Act
        var act = () => this.CreateWindow(session, scope => scope.Register<IService, OtherService>(Reuse.Singleton));

        // Assert
        _ = act.Should().Throw<UndeclaredShadowException>();
    }

    [TestMethod]
    public async Task Create_ShadowOfSameKey_Throws()
    {
        // Arrange
        using var host = TestScopes.CreateHost(root => root.Register<IService, Service>(Reuse.Singleton, serviceKey: "key"));
        await using var root = ScopeFactory.CreateRoot(host);

        // Act
        var act = () => this.CreateWindow(root, scope => scope.Register<IService, OtherService>(Reuse.Singleton, serviceKey: "key"));

        // Assert
        _ = act.Should().Throw<UndeclaredShadowException>()
            .Which.Registrations.Should().ContainSingle()
            .Which.OptionalServiceKey.Should().Be("key");
    }

    [TestMethod]
    [DataRow("other key", DisplayName = "another key")]
    [DataRow(null, DisplayName = "the default registration")]
    public async Task Create_RegistrationOfAKeyTheParentLacks_IsNotAShadow(string? key)
    {
        // Arrange
        using var host = TestScopes.CreateHost(root => root.Register<IService, Service>(Reuse.Singleton, serviceKey: "key"));
        await using var root = ScopeFactory.CreateRoot(host);

        // Act
        var window = this.CreateWindow(root, scope => scope.Register<IService, OtherService>(Reuse.Singleton, serviceKey: key));

        // Assert
        _ = window.Resolver.Resolve<IService>(key).Should().BeOfType<OtherService>();
    }

    [TestMethod]
    public async Task Create_ClosedRegistrationOverRootOpenGeneric_Throws()
    {
        // Arrange
        using var host = TestScopes.CreateHost(root => root.Register(typeof(IGenericService<>), typeof(GenericService<>), Reuse.Singleton));
        await using var root = ScopeFactory.CreateRoot(host);

        // Act
        var act = () => this.CreateWindow(root, scope => scope.Register<IGenericService<int>, GenericService<int>>(Reuse.Singleton));

        // Assert
        _ = act.Should().Throw<UndeclaredShadowException>()
            .Which.Registrations.Should().ContainSingle()
            .Which.ServiceType.Should().Be<IGenericService<int>>();
    }

    [TestMethod]
    public async Task Create_OpenGenericOverRootClosedRegistration_Throws()
    {
        // Arrange
        using var host = TestScopes.CreateHost(root => root.Register<IGenericService<int>, GenericService<int>>(Reuse.Singleton));
        await using var root = ScopeFactory.CreateRoot(host);

        // Act
        var act = () => this.CreateWindow(root, scope => scope.Register(typeof(IGenericService<>), typeof(GenericService<>), Reuse.Singleton));

        // Assert
        _ = act.Should().Throw<UndeclaredShadowException>()
            .Which.Registrations.Should().ContainSingle()
            .Which.ServiceType.Should().Be(typeof(IGenericService<>));
    }

    [TestMethod]
    public async Task Create_ShadowDeclaredByGenericDefinition_CoversTheConstructedType()
    {
        // Arrange
        using var host = TestScopes.CreateHost(root => root.Register(typeof(IGenericService<>), typeof(GenericService<>), Reuse.Singleton));
        await using var root = ScopeFactory.CreateRoot(host);

        // Act
        var window = this.CreateWindow(
            root,
            scope => scope.Register<IGenericService<int>, GenericService<int>>(Reuse.Singleton),
            typeof(IGenericService<>));

        // Assert
        _ = window.Resolver.Resolve<IGenericService<int>>().Should().NotBeSameAs(host.Resolve<IGenericService<int>>());
    }

    [TestMethod]
    public async Task Create_UndeclaredShadowOfHostInstance_Throws()
    {
        // Arrange
        using var host = TestScopes.CreateHost(root => root.Register<IService, Service>(Reuse.Singleton));
        await using var root = ScopeFactory.CreateRoot(host);

        // Act
        var act = () => this.factory.Create<IWindowLayer>(
            root,
            [new TestModule<IWindowLayer>(_ => { })],
            scope => scope.RegisterInstance<IService>(new OtherService()));

        // Assert
        _ = act.Should().Throw<UndeclaredShadowException>();
    }

    [TestMethod]
    public async Task Create_UndeclaredShadow_LeavesTheParentWithoutTheScope()
    {
        // Arrange
        using var host = TestScopes.CreateHost(root => root.Register<IService, Service>(Reuse.Singleton));
        var root = ScopeFactory.CreateRoot(host);
        var log = new EventLog();
        root.Own(new AsyncRecorder(log, "root"));
        var create = () => this.CreateWindow(root, scope => scope.Register<IService, OtherService>(Reuse.Singleton));
        _ = create.Should().Throw<UndeclaredShadowException>();

        // Act
        await root.DisposeAsync().ConfigureAwait(true);

        // Assert
        _ = log.Events.Should().Equal("disposed root");
        _ = host.Resolve<IService>().Should().BeOfType<Service>();
    }

    [TestMethod]
    public async Task FindUndeclaredShadows_DeclaredAndUndeclared_ReturnsOnlyTheUndeclared()
    {
        // Arrange
        using var host = TestScopes.CreateHost(root =>
        {
            root.Register<IService, Service>(Reuse.Singleton);
            root.Register<Consumer>(Reuse.Transient);
        });
        await using var root = ScopeFactory.CreateRoot(host);
        using var scope = TestScopes.CreateHost(registrations =>
        {
            registrations.Register<IService, OtherService>(Reuse.Singleton);
            registrations.Register<Consumer>(Reuse.Transient);
            registrations.Register<OptionalConsumer>(Reuse.Transient);
        });

        // Act
        var undeclared = ScopeRegistrations.FindUndeclaredShadows(scope, root, [typeof(IService)]);

        // Assert
        _ = undeclared.Should().ContainSingle().Which.ServiceType.Should().Be<Consumer>();
    }

    private ScopeHandle CreateWindow(ScopeHandle parent, Action<IRegistrator> register, params Type[] shadows)
        => this.factory.Create<IWindowLayer>(parent, [new TestModule<IWindowLayer>(register) { Shadows = shadows }]);
}
