// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

public class NoRouteForSegmentsException(IEnumerable<UrlSegment> segments, UrlSegmentGroup root)
    : Exception($"no route matched segments `{string.Join('/', segments)}` while processing the url tree `{root}`");
