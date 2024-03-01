// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Thrown when no <see cref="IRoute">route</see> was found in the <see cref="IRouter">router</see> configuration, which path
/// matches the path specified by the segments that were being recognized.
/// </summary>
/// <param name="segments">The segments of the path being recognized.</param>
/// <param name="root">The root of the pared <see cref="IUrlTree"/> to which the <paramref name="segments"/> belong.</param>
public class NoRouteForSegmentsException(IEnumerable<IUrlSegment> segments, IUrlSegmentGroup root)
    : ApplicationException($"no route matched segments `{string.Join('/', segments)}` while processing the url tree `{root}`");
