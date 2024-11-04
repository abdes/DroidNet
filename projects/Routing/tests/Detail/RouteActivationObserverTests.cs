// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Tests.Detail;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Routing.Detail;
using DryIoc;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;
using Parameters = DroidNet.Routing.Parameters;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(RouteActivationObserver))]
public class RouteActivationObserverTests
{
    private readonly Mock<IContainer> containerMock;
    private readonly RouteActivationObserver observer;

    public RouteActivationObserverTests()
    {
        this.containerMock = new Mock<IContainer>();
        this.observer = new RouteActivationObserver(this.containerMock.Object);
    }

    [TestMethod]
    public void OnActivating_WithInvalidRouteType_ThrowsArgumentException()
    {
        // Arrange
        var route = new Mock<IActiveRoute>().Object;
        var context = new Mock<INavigationContext>().Object;

        // Act
        Action act = () => this.observer.OnActivating(route, context);

        // Assert
        act.Should()
            .Throw<ArgumentException>()
            .WithMessage("*Expected route `route` to be of type `*ActiveRoute`*");
    }

    [TestMethod]
    public void OnActivating_WithAlreadyActivatedRoute_ReturnsFalse()
    {
        // Arrange
        var route = MakeActiveRouteNoViewModel();
        route.IsActivated = true;
        var context = new Mock<INavigationContext>().Object;

        // Act
        var result = this.observer.OnActivating(route, context);

        // Assert
        result.Should().BeFalse();
    }

    [TestMethod]
    public void OnActivating_WithRouteWithoutViewModelType_ReturnsTrue()
    {
        // Arrange
        var route = MakeActiveRouteNoViewModel();
        var context = new Mock<INavigationContext>().Object;

        // Act
        var result = this.observer.OnActivating(route, context);

        // Assert
        result.Should().BeTrue();
    }

    [TestMethod]
    public void OnActivating_WithRoutingAwareViewModelType_SetsViewModelAndInjectsActiveRoute()
    {
        // Arrange
        var route = MakeActiveRouteWithViewModel(typeof(MockViewModel));
        var context = new Mock<INavigationContext>().Object;

        var viewModelMock = new Mock<IRoutingAware>();
        this.containerMock.Setup(c => c.GetService(It.IsAny<Type>())).Returns(viewModelMock.Object);

        // Act
        var result = this.observer.OnActivating(route, context);

        // Assert
        result.Should().BeTrue();
        route.ViewModel.Should().Be(viewModelMock.Object);
        viewModelMock.VerifySet(vm => vm.ActiveRoute = route, Times.Once);
    }

    [TestMethod]
    public void OnActivating_WithMissingViewModel_ThrowsMissingViewModelException()
    {
        // Arrange
        var route = MakeActiveRouteWithViewModel(typeof(MockViewModel));
        var context = new Mock<INavigationContext>().Object;

        // Act
        Action act = () => this.observer.OnActivating(route, context);

        // Assert
        act.Should()
            .Throw<MissingViewModelException>()
            .And.ViewModelType.Should()
            .Be(typeof(MockViewModel));
    }

    [TestMethod]
    public void OnActivated_SetsRouteAsActivated()
    {
        // Arrange
        var route = MakeActiveRouteNoViewModel();
        route.IsActivated.Should().BeFalse();
        var context = new Mock<INavigationContext>().Object;

        // Act
        this.observer.OnActivated(route, context);

        // Assert
        route.IsActivated.Should().BeTrue();
    }

    private static ActiveRoute MakeActiveRouteNoViewModel() =>
        new()
        {
            Outlet = "test",
            Params = new Parameters(),
            QueryParams = new Parameters(),
            UrlSegments = [],
            UrlSegmentGroup = new UrlSegmentGroup([]),
            IsActivated = false,
            RouteConfig = new Route { ViewModelType = null },
        };

    private static ActiveRoute MakeActiveRouteWithViewModel(Type viewModelType) =>
        new()
        {
            Outlet = "test",
            Params = new Parameters(),
            QueryParams = new Parameters(),
            UrlSegments = [],
            UrlSegmentGroup = new UrlSegmentGroup([]),
            IsActivated = false,
            RouteConfig = new Route { ViewModelType = viewModelType },
        };

    /// <summary>
    /// Used for mocking. Must be public.
    /// </summary>
    /// ReSharper disable once MemberCanBePrivate.Global
    public static class MockViewModel;
}
