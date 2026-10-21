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
///     A new scope creates its modules' activation lists, module by module and in order, once every
///     module has registered; a failed activation leaves nothing behind.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Scope activation")]
public sealed class ScopeActivationTests
{
    private readonly ScopeFactory factory = new();

    [TestMethod]
    public async Task Create_ActivationLists_CreateServicesInModuleThenListOrder()
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        await using var root = ScopeFactory.CreateRoot(host);
        var log = new EventLog();
        IScopeModule<IWindowLayer>[] modules =
        [
            new TestModule<IWindowLayer>(scope => scope.Register<SecondRecorder>(Reuse.Singleton)) { Activation = [typeof(SecondRecorder)] },
            new TestModule<IWindowLayer>(scope =>
            {
                scope.Register<FirstRecorder>(Reuse.Singleton);
                scope.RegisterDelegate(_ => new NamedRecorder(log, "third"), Reuse.Singleton);
            })
            {
                Activation = [typeof(NamedRecorder), typeof(FirstRecorder)],
            },
        ];

        // Act
        _ = this.factory.Create(root, modules, scope => scope.RegisterInstance(log));

        // Assert
        _ = log.Events.Should().Equal("created second", "created third", "created first");
    }

    [TestMethod]
    public async Task Create_ActivationOfAnotherModuleRegistration_CreatesIt()
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        await using var root = ScopeFactory.CreateRoot(host);
        var log = new EventLog();
        IScopeModule<IWindowLayer>[] modules =
        [
            new TestModule<IWindowLayer>(_ => { }) { Activation = [typeof(FirstRecorder)] },
            new TestModule<IWindowLayer>(scope => scope.Register<FirstRecorder>(Reuse.Singleton)),
        ];

        // Act
        var window = this.factory.Create(root, modules, scope => scope.RegisterInstance(log));

        // Assert
        _ = log.Events.Should().Equal("created first");
        _ = window.Resolver.Resolve<FirstRecorder>().Should().NotBeNull();
    }

    [TestMethod]
    public async Task Create_ActivationFails_DisposesWhatTheScopeCreatedAndLeavesTheParent()
    {
        // Arrange
        using var host = TestScopes.CreateHost();
        var root = ScopeFactory.CreateRoot(host);
        var log = new EventLog();
        root.Own(new AsyncRecorder(log, "root"));
        IScopeModule<IWindowLayer>[] modules =
        [
            new TestModule<IWindowLayer>(scope =>
            {
                scope.Register<FirstRecorder>(Reuse.Singleton);
                scope.Register<FailingService>(Reuse.Singleton);
            })
            {
                Activation = [typeof(FirstRecorder), typeof(FailingService)],
            },
        ];

        // Act
        var create = () => this.factory.Create(root, modules, scope => scope.RegisterInstance(log));

        // Assert
        _ = create.Should().Throw<InvalidOperationException>();
        _ = log.Events.Should().Equal("created first", "disposed first");
        await root.DisposeAsync().ConfigureAwait(true);
        _ = log.Events.Should().Equal("created first", "disposed first", "disposed root");
    }
}
