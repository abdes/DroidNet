// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Routing.Detail;
using DryIoc;
using FluentAssertions;
using Moq;

namespace DroidNet.Routing.Tests.Detail;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(RouteActivationObserver))]
public class RouteActivationObserverTests
{
    private readonly Mock<IContainer> containerMock;
    private readonly RouteActivationObserver observer;

    /// <summary>
    /// Initializes a new instance of the <see cref="RouteActivationObserverTests"/> class.
    /// </summary>
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
        _ = act.Should()
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
        _ = result.Should().BeFalse();
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
        _ = result.Should().BeTrue();
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
        _ = act.Should()
            .Throw<MissingViewModelException>()
            .And.ViewModelType.Should()
            .Be(typeof(MockViewModel));
    }

    [TestMethod]
    public async Task OnActivated_SetsRouteAsActivated()
    {
        // Arrange
        var route = MakeActiveRouteNoViewModel();
        _ = route.IsActivated.Should().BeFalse();
        var context = new Mock<INavigationContext>().Object;

        // Act
        await this.observer.OnActivatedAsync(route, context).ConfigureAwait(false);

        // Assert
        _ = route.IsActivated.Should().BeTrue();
    }

    private static ActiveRoute MakeActiveRouteNoViewModel() =>
        new()
        {
            Outlet = "test",
            Params = new Parameters(),
            QueryParams = new Parameters(),
            Segments = [],
            SegmentGroup = new UrlSegmentGroup([]),
            IsActivated = false,
            Config = new Route { ViewModelType = null },
        };

    private static ActiveRoute MakeActiveRouteWithViewModel(Type viewModelType) =>
        new()
        {
            Outlet = "test",
            Params = new Parameters(),
            QueryParams = new Parameters(),
            Segments = [],
            SegmentGroup = new UrlSegmentGroup([]),
            IsActivated = false,
            Config = new Route { ViewModelType = viewModelType },
        };

    /// <summary>
    /// Used for mocking. Must be public.
    /// </summary>
    /// ReSharper disable once MemberCanBePrivate.Global
    internal static class MockViewModel;
}
