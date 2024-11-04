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
    private readonly Mock<DummyRouteActivator> activatorMock;
    private readonly AbstractRouteActivator activator;

    public RouteActivatorTests()
    {
        this.activatorMock = new Mock<DummyRouteActivator> { CallBase = true };

        this.activator = this.activatorMock.Object;
    }

    /// <summary>
    /// The root node in the router state is just a holder for the actual routes
    /// to be activated. It should never be activated.
    /// </summary>
    [TestMethod]
    public void RootNode_ShouldNotBeActivated()
    {
        // Setup
        var routeMock = new Mock<IActiveRoute>() { CallBase = true };
        routeMock.SetupGet(r => r.Parent).Returns((IActiveRoute?)null);
        routeMock.SetupGet(r => r.Children).Returns([]);
        var contextMock = new Mock<INavigationContext>();

        // Act
        var result = this.activator.ActivateRoutesRecursive(routeMock.Object, contextMock.Object);

        // Assert
        result.Should().BeTrue();
        this.activatorMock.Protected()
            .Verify("DoActivateRoute", Times.Never(), routeMock.Object, contextMock.Object);
    }

    [TestMethod]
    public void ActivateRoutesRecursive_ShouldActivateChildRoutes()
    {
        // Arrange
        var (rootNode, childRoute) = MakeRootNodeWithChild();

        var contextMock = new Mock<INavigationContext>();
        contextMock.SetupGet(c => c.RouteActivationObserver).Returns((IRouteActivationObserver?)null);

        // Act
        var result = this.activator.ActivateRoutesRecursive(rootNode, contextMock.Object);

        // Assert
        result.Should().BeTrue();
        this.activatorMock.Protected()
            .Verify("DoActivateRoute", Times.Once(), childRoute, contextMock.Object);
    }

    [TestMethod]
    public void ActivateRoute_WithObserverAcceptsActivation_ShouldProceed()
    {
        // Arrange
        var (rootNode, childRoute) = MakeRootNodeWithChild();

        var observerMock = new Mock<IRouteActivationObserver>();
        var contextMock = new Mock<INavigationContext>();
        contextMock.SetupGet(c => c.RouteActivationObserver).Returns(observerMock.Object);
        observerMock.Setup(o => o.OnActivating(childRoute, contextMock.Object)).Returns(value: true);

        // Act
        var result = this.activator.ActivateRoutesRecursive(rootNode, contextMock.Object);

        // Assert
        observerMock.VerifyAll();
        result.Should().BeTrue();
        this.activatorMock.Protected()
            .Verify("DoActivateRoute", Times.Once(), childRoute, contextMock.Object);
    }

    [TestMethod]
    public void ActivateRoute_WithObserverRejectsActivation_ShouldNotProceed()
    {
        // Arrange
        var (rootNode, childRoute) = MakeRootNodeWithChild();

        var observerMock = new Mock<IRouteActivationObserver>();
        var contextMock = new Mock<INavigationContext>();
        contextMock.SetupGet(c => c.RouteActivationObserver).Returns(observerMock.Object);
        observerMock.Setup(o => o.OnActivating(childRoute, contextMock.Object)).Returns(value: false);

        // Act
        var result = this.activator.ActivateRoutesRecursive(rootNode, contextMock.Object);

        // Assert
        observerMock.VerifyAll();
        result.Should().BeFalse();
        this.activatorMock.Protected()
            .Verify("DoActivateRoute", Times.Never(), childRoute, contextMock.Object);
    }

    [TestMethod]
    public void ActivateRoute_WithObserverAcceptsActivation_ShouldInvokeObserverOnActivated()
    {
        // Arrange
        var (rootNode, childRoute) = MakeRootNodeWithChild();

        var observerMock = new Mock<IRouteActivationObserver>();
        var contextMock = new Mock<INavigationContext>();
        contextMock.SetupGet(c => c.RouteActivationObserver).Returns(observerMock.Object);
        observerMock.Setup(o => o.OnActivating(childRoute, contextMock.Object)).Returns(value: true);
        observerMock.Setup(o => o.OnActivated(childRoute, contextMock.Object));

        // Act
        var result = this.activator.ActivateRoutesRecursive(rootNode, contextMock.Object);

        // Assert
        result.Should().BeTrue();
        this.activatorMock.Protected()
            .Verify("DoActivateRoute", Times.Once(), childRoute, contextMock.Object);
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
