// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>Represents the type of parameter in a URL.</summary>
internal enum ParamType
{
    /// <summary>
    /// Represents a matrix parameter in a URL. Matrix parameters appear in the
    /// URL path segment and are separated by semicolons.
    /// </summary>
    Matrix = 1,

    /// <summary>
    /// Represents a query parameter in a URL. Query parameters appear after the
    /// question mark in a URL and are separated by ampersands.
    /// </summary>
    Query = 2,
}
