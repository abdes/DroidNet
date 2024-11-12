// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Tests;

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;
using Moq.Protected;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Route Activation")]
public class RouteActivatorTests
{
    private const string TestTarget = "test-target";
    private readonly Mock<DummyRouteActivator> activatorMock;
    private readonly AbstractRouteActivator activator;
    private readonly Mock<NavigationContext> contextMock = new(Target.Main, TestTarget);

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
        this.contextMock.SetupGet(c => c.RouteActivationObserver).Returns(observerMock.Object);
        observerMock.Setup(o => o.OnActivating(rootNode, this.contextMock.Object)).Throws<InvalidOperationException>();

        // Act
        var result = this.activator.ActivateRoutesRecursive(rootNode, this.contextMock.Object);

        // Assert
        result.Should().BeFalse();
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

        this.contextMock.SetupGet(c => c.RouteActivationObserver).Returns((IRouteActivationObserver?)null);

        // Act
        var result = this.activator.ActivateRoutesRecursive(rootNode, this.contextMock.Object);

        // Assert
        result.Should().BeTrue();
        this.activatorMock.Protected()
            .Verify("DoActivateRoute", Times.Once(), childRoute, this.contextMock.Object);
    }

    [TestMethod]
    public void ActivateRoute_WithObserverAcceptsActivation_ShouldProceed()
    {
        // Arrange
        var (rootNode, childRoute) = MakeRootNodeWithChild();

        var observerMock = new Mock<IRouteActivationObserver>();
        this.contextMock.SetupGet(c => c.RouteActivationObserver).Returns(observerMock.Object);
        observerMock.Setup(o => o.OnActivating(rootNode, this.contextMock.Object)).Returns(value: true);
        observerMock.Setup(o => o.OnActivating(childRoute, this.contextMock.Object)).Returns(value: true);

        // Act
        var result = this.activator.ActivateRoutesRecursive(rootNode, this.contextMock.Object);

        // Assert
        observerMock.VerifyAll();
        result.Should().BeTrue();
        this.activatorMock.Protected()
            .Verify("DoActivateRoute", Times.Once(), childRoute, this.contextMock.Object);
    }

    [TestMethod]
    public void ActivateRoute_WithObserverRejectsActivation_ShouldNotProceed()
    {
        // Arrange
        var (rootNode, childRoute) = MakeRootNodeWithChild();

        var observerMock = new Mock<IRouteActivationObserver>();
        this.contextMock.SetupGet(c => c.RouteActivationObserver).Returns(observerMock.Object);
        observerMock.Setup(o => o.OnActivating(rootNode, this.contextMock.Object)).Returns(value: true);
        observerMock.Setup(o => o.OnActivating(childRoute, this.contextMock.Object)).Returns(value: false);

        // Act
        var result = this.activator.ActivateRoutesRecursive(rootNode, this.contextMock.Object);

        // Assert
        observerMock.VerifyAll();
        result.Should().BeTrue();
        this.activatorMock.Protected()
            .Verify("DoActivateRoute", Times.Never(), childRoute, this.contextMock.Object);
    }

    [TestMethod]
    public void ActivateRoute_WithObserverAcceptsActivation_ShouldInvokeObserverOnActivated()
    {
        // Arrange
        var (rootNode, childRoute) = MakeRootNodeWithChild();

        var observerMock = new Mock<IRouteActivationObserver>();
        this.contextMock.SetupGet(c => c.RouteActivationObserver).Returns(observerMock.Object);
        observerMock.Setup(o => o.OnActivating(rootNode, this.contextMock.Object)).Returns(value: true);
        observerMock.Setup(o => o.OnActivating(childRoute, this.contextMock.Object)).Returns(value: true);
        observerMock.Setup(o => o.OnActivated(rootNode, this.contextMock.Object));
        observerMock.Setup(o => o.OnActivated(childRoute, this.contextMock.Object));

        // Act
        var result = this.activator.ActivateRoutesRecursive(rootNode, this.contextMock.Object);

        // Assert
        result.Should().BeTrue();
        observerMock.Verify(o => o.OnActivated(rootNode, this.contextMock.Object), Times.Once());
        observerMock.Verify(o => o.OnActivated(childRoute, this.contextMock.Object), Times.Once());
    }

    private static (IActiveRoute rootNode, IActiveRoute childRoute) MakeRootNodeWithChild()
    {
        var rootNodeMock = new Mock<IActiveRoute>();
        rootNodeMock.SetupGet(r => r.Parent).Returns((IActiveRoute?)null);

        var childMock = new Mock<IActiveRoute>();
        childMock.SetupGet(r => r.Parent).Returns(rootNodeMock.Object);
        childMock.SetupGet(r => r.Children).Returns([]);
        rootNodeMock.SetupGet(r => r.Children).Returns([childMock.Object]);
        return (rootNodeMock.Object, childMock.Object);
    }
}

[ExcludeFromCodeCoverage]
public abstract class DummyRouteActivator : AbstractRouteActivator
{
    protected override void DoActivateRoute(IActiveRoute route, INavigationContext context)
    {
    }
}
