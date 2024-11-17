// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Moq;
using Moq.Protected;

namespace DroidNet.Routing.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Route Activation")]
public class RouteActivatorTests
{
    private const string TestTarget = "test-target";
    private readonly Mock<DummyRouteActivator> activatorMock;
    private readonly AbstractRouteActivator activator;
    private readonly Mock<NavigationContext> contextMock = new(Target.Main, TestTarget);

    /// <summary>
    /// Initializes a new instance of the <see cref="RouteActivatorTests"/> class.
    /// </summary>
    public RouteActivatorTests()
    {
        this.activatorMock = new Mock<DummyRouteActivator> { CallBase = true };

        this.activator = this.activatorMock.Object;
    }

    [TestMethod]
    public void ActivateRoutesRecursive_WhenOneActivationFails_ShouldFail()
    {
        // Arrange
        var (rootNode, childRoute) = MakeRootNodeWithChild();

        var observerMock = new Mock<IRouteActivationObserver>();
        _ = this.contextMock.SetupGet(c => c.RouteActivationObserver).Returns(observerMock.Object);
        _ = observerMock.Setup(o => o.OnActivating(rootNode, this.contextMock.Object)).Throws<InvalidOperationException>();

        // Act
        var result = this.activator.ActivateRoutesRecursive(rootNode, this.contextMock.Object);

        // Assert
        _ = result.Should().BeFalse();
        this.activatorMock.Protected()
            .Verify("DoActivateRoute", Times.Never(), rootNode, this.contextMock.Object);
        this.activatorMock.Protected()
            .Verify("DoActivateRoute", Times.Never(), childRoute, this.contextMock.Object);
    }

    [TestMethod]
    public void ActivateRoutesRecursive_ShouldActivateChildRoutes()
    {
        // Arrange
        var (rootNode, childRoute) = MakeRootNodeWithChild();

        _ = this.contextMock.SetupGet(c => c.RouteActivationObserver).Returns((IRouteActivationObserver?)null);

        // Act
        var result = this.activator.ActivateRoutesRecursive(rootNode, this.contextMock.Object);

        // Assert
        _ = result.Should().BeTrue();
        this.activatorMock.Protected()
            .Verify("DoActivateRoute", Times.Once(), childRoute, this.contextMock.Object);
    }

    [TestMethod]
    public void ActivateRoute_WithObserverAcceptsActivation_ShouldProceed()
    {
        // Arrange
        var (rootNode, childRoute) = MakeRootNodeWithChild();

        var observerMock = new Mock<IRouteActivationObserver>();
        _ = this.contextMock.SetupGet(c => c.RouteActivationObserver).Returns(observerMock.Object);
        _ = observerMock.Setup(o => o.OnActivating(rootNode, this.contextMock.Object)).Returns(value: true);
        _ = observerMock.Setup(o => o.OnActivating(childRoute, this.contextMock.Object)).Returns(value: true);

        // Act
        var result = this.activator.ActivateRoutesRecursive(rootNode, this.contextMock.Object);

        // Assert
        observerMock.VerifyAll();
        _ = result.Should().BeTrue();
        this.activatorMock.Protected()
            .Verify("DoActivateRoute", Times.Once(), childRoute, this.contextMock.Object);
    }

    [TestMethod]
    public void ActivateRoute_WithObserverRejectsActivation_ShouldNotProceed()
    {
        // Arrange
        var (rootNode, childRoute) = MakeRootNodeWithChild();

        var observerMock = new Mock<IRouteActivationObserver>();
        _ = this.contextMock.SetupGet(c => c.RouteActivationObserver).Returns(observerMock.Object);
        _ = observerMock.Setup(o => o.OnActivating(rootNode, this.contextMock.Object)).Returns(value: true);
        _ = observerMock.Setup(o => o.OnActivating(childRoute, this.contextMock.Object)).Returns(value: false);

        // Act
        var result = this.activator.ActivateRoutesRecursive(rootNode, this.contextMock.Object);

        // Assert
        observerMock.VerifyAll();
        _ = result.Should().BeTrue();
        this.activatorMock.Protected()
            .Verify("DoActivateRoute", Times.Never(), childRoute, this.contextMock.Object);
    }

    [TestMethod]
    public void ActivateRoute_WithObserverAcceptsActivation_ShouldInvokeObserverOnActivated()
    {
        // Arrange
        var (rootNode, childRoute) = MakeRootNodeWithChild();

        var observerMock = new Mock<IRouteActivationObserver>();
        _ = this.contextMock.SetupGet(c => c.RouteActivationObserver).Returns(observerMock.Object);
        _ = observerMock.Setup(o => o.OnActivating(rootNode, this.contextMock.Object)).Returns(value: true);
        _ = observerMock.Setup(o => o.OnActivating(childRoute, this.contextMock.Object)).Returns(value: true);
        _ = observerMock.Setup(o => o.OnActivated(rootNode, this.contextMock.Object));
        _ = observerMock.Setup(o => o.OnActivated(childRoute, this.contextMock.Object));

        // Act
        var result = this.activator.ActivateRoutesRecursive(rootNode, this.contextMock.Object);

        // Assert
        _ = result.Should().BeTrue();
        observerMock.Verify(o => o.OnActivated(rootNode, this.contextMock.Object), Times.Once());
        observerMock.Verify(o => o.OnActivated(childRoute, this.contextMock.Object), Times.Once());
    }

    private static (IActiveRoute rootNode, IActiveRoute childRoute) MakeRootNodeWithChild()
    {
        var rootNodeMock = new Mock<IActiveRoute>();
        _ = rootNodeMock.SetupGet(r => r.Parent).Returns((IActiveRoute?)null);

        var childMock = new Mock<IActiveRoute>();
        _ = childMock.SetupGet(r => r.Parent).Returns(rootNodeMock.Object);
        _ = childMock.SetupGet(r => r.Children).Returns([]);
        _ = rootNodeMock.SetupGet(r => r.Children).Returns([childMock.Object]);
        return (rootNodeMock.Object, childMock.Object);
    }
}

[ExcludeFromCodeCoverage]
[SuppressMessage("StyleCop.CSharp.MaintainabilityRules", "SA1402:File may only contain a single type", Justification = "class only using inside these unit tests")]
public abstract class DummyRouteActivator : AbstractRouteActivator
{
    /// <inheritdoc/>
    protected override void DoActivateRoute(IActiveRoute route, INavigationContext context)
    {
    }
}
