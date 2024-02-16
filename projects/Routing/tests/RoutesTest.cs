// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

/// <summary>Unit tests for the Routes class.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
public class RoutesTest
{
    private readonly Mock<IRouteValidator> validator;
    private readonly Routes routes;

    /// <summary>
    /// Initializes a new instance of the <see cref="RoutesTest" /> class.
    /// </summary>
    /// <remarks>
    /// A new instance of the test class is created for each test case.
    /// </remarks>
    public RoutesTest()
    {
        this.validator = new Mock<IRouteValidator>();
        this.routes = new Routes([], this.validator.Object);
    }

    /// <summary>
    /// Tests that a valid route is properly added to the list of configured
    /// routes.
    /// </summary>
    [TestMethod]
    public void Add_WithValidRoute_AddsIt()
    {
        _ = this.validator.Setup(v => v.ValidateRoute(It.IsAny<Routes>(), It.IsAny<Route>()));
        var route = new Route() { Path = "test" };

        var action = () => this.routes.Add(route);

        _ = action.Should().NotThrow();
        this.validator.Verify(v => v.ValidateRoute(this.routes, route));
        _ = this.routes.Should().HaveCount(1).And.Contain(route);
    }

    /// <summary>
    /// Tests that given a route that fails validation, it is not added to the
    /// list of configured routes and a <see cref="RoutesConfigurationException" />
    /// is thrown.
    /// </summary>
    [TestMethod]
    public void Add_WithInvalidRoute_ThrowsRoutesConfigurationException()
    {
        var route = new Route() { Path = "test" };

        _ = this.validator.Setup(v => v.ValidateRoute(It.IsAny<Routes>(), It.IsAny<Route>()))
            .Throws(() => new RoutesConfigurationException(string.Empty, route));

        var action = () => this.routes.Add(route);

        _ = action.Should().Throw<RoutesConfigurationException>();
        this.validator.Verify(v => v.ValidateRoute(this.routes, route));
        _ = this.routes.Should().BeEmpty();
    }

    /// <summary>
    /// Tests that given a collection of valid routes, when added, all of them
    /// will be present in the routes list.
    /// </summary>
    [TestMethod]
    public void AddRange_WithValidRoutes_AddsAll()
    {
        _ = this.validator.Setup(v => v.ValidateRoute(It.IsAny<Routes>(), It.IsAny<Route>()));
        var newRoutes = new Route[]
        {
            new() { Path = "route1" }, new() { Path = "route2" }, new() { Path = "route3" },
        };

        var action = () => this.routes.AddRange(newRoutes);

        _ = action.Should().NotThrow();
        _ = this.routes.Should().HaveCount(newRoutes.Length).And.Contain(newRoutes);
    }

    /// <summary>
    /// Tests that given a collection of valid routes, when added, all of them
    /// will be present in the routes list.
    /// </summary>
    [TestMethod]
    public void AddRange_WithSomeInvalidRoutes_ThrowsRoutesConfigurationException()
    {
        var validRoute = new Route() { Path = "valid" };
        var invalidRoute = new Route() { Path = "invalid" };
        _ = this.validator.Setup(v => v.ValidateRoute(It.IsAny<Routes>(), validRoute));
        _ = this.validator.Setup(v => v.ValidateRoute(It.IsAny<Routes>(), validRoute))
            .Throws(new RoutesConfigurationException("invalid", invalidRoute));

        var action = () => this.routes.AddRange([validRoute, invalidRoute]);

        _ = action.Should()
            .Throw<RoutesConfigurationException>()
            .Which.FailedRoute.Should()
            .Be(invalidRoute);
    }

    /// <summary>
    /// Tests that the SortedByMatchingOutlet method returns routes sorted by
    /// the specified outlet. The routes with the specified outlet should come
    /// first in the same order they were in the original list, followed by the
    /// remaining routes in their original order.
    /// </summary>
    [TestMethod]
    public void SortedByMatchingOutlet_WhenCalledWithOutlet_ReturnsRoutesSortedByOutlet()
    {
        var route1 = new Route
        {
            Path = "path1",
            Outlet = "outlet1",
        };
        var route2 = new Route
        {
            Path = "path2",
            Outlet = "outlet2",
        };
        var route3 = new Route
        {
            Path = "path3",
            Outlet = "outlet3",
        };
        var route4 = new Route
        {
            Path = "path4",
            Outlet = "outlet1",
        };
        this.routes.AddRange(
            new List<Route>
            {
                route1,
                route2,
                route3,
                route4,
            });

        var result = this.routes.SortedByMatchingOutlet("outlet1");

        // Preserves the order of the routes
        _ = result.Should().HaveCount(this.routes.Count).And.ContainInConsecutiveOrder(route1, route4, route2, route3);
    }

    /// <summary>
    /// Tests that the SortedByMatchingOutlet method returns routes in their
    /// original order when called with a non-existent outlet.
    /// </summary>
    [TestMethod]
    public void SortedByMatchingOutlet_WhenCalledWithNonExistentOutlet_ReturnsRoutesInOriginalOrder()
    {
        _ = this.validator.Setup(v => v.ValidateRoute(It.IsAny<Routes>(), It.IsAny<Route>()));
        var route1 = new Route
        {
            Path = "path1",
            Outlet = "outlet1",
        };
        var route2 = new Route
        {
            Path = "path2",
            Outlet = "outlet2",
        };
        var route3 = new Route
        {
            Path = "path3",
            Outlet = "outlet1",
        };
        this.routes.AddRange(
            new List<Route>
            {
                route1,
                route2,
                route3,
            });

        var result = this.routes.SortedByMatchingOutlet("outlet3");

        _ = result.Should().HaveCount(this.routes.Count).And.ContainInConsecutiveOrder(route1, route2, route3);
    }
}
