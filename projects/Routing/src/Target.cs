// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Contains the definition of the special routing target names.
/// </summary>
public static class Target
{
    /// <summary>
    /// A special routing target name, used to refer to the main top level
    /// target.
    /// </summary>
    /// <remarks>
    /// There can only be one <see cref="RouterContext" /> for the main target.
    /// </remarks>
    public const string Main = "_main";

    /// <summary>
    /// A special routing target name, used to refer to <see cref="RouterContext" /> of
    /// the current <see cref="ActiveRoute" />.
    /// </summary>
    /// <remarks>
    /// When a target is not specified for a navigation request, it is assumed
    /// to be <see cref="Self" />.
    /// </remarks>
    public const string Self = "_self";
}
