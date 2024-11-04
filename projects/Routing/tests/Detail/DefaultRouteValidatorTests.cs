// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Tests.Detail;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Routing.Detail;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>
/// Contains test cases for the <see cref="DefaultRouteValidator" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(DefaultRouteValidator))]
public class DefaultRouteValidatorTests
{
    private readonly Routes routes = new([]);
    private readonly IRouteValidator validator = DefaultRouteValidator.Instance;

    /// <summary>Test validation success.</summary>
    [TestMethod]
    public void ValidateRoute_WithValidRoute_Works()
    {
        var route = new Route { Path = "about" };

        var act = () => this.validator.ValidateRoute(this.routes, route);

        _ = act.Should().NotThrow();
    }

    /// <summary>
    /// Test validation error when the route's Matcher is DefaultMatcher but
    /// the route's Path is null.
    /// </summary>
    [TestMethod]
    public void ValidateRoute_WithDefaultMatcherAdnNullPath_Throws()
    {
        var route = new Route { Path = null };

        var act = () => this.validator.ValidateRoute(this.routes, route);

        _ = act.Should()
            .Throw<RoutesConfigurationException>("when using the default matcher, a route must specify a path")
            .Which.FailedRoute.Should()
            .Be(route);
    }

    /// <summary>Test validation error when the Path starts with '/'.</summary>
    [TestMethod]
    public void ValidateRoute_WhenPathStartsWithSlash_Throws()
    {
        var route = new Route { Path = "/" };

        var act = () => this.validator.ValidateRoute(this.routes, route);

        _ = act.Should()
            .Throw<RoutesConfigurationException>("the path of a route should not start with '/'")
            .Which.FailedRoute.Should()
            .Be(route);
    }
}
