// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Routing;

namespace Oxygen.Editor.Routing;

/// <summary>
/// Represents the local router context within the World Editor.
/// </summary>
/// <remarks>
/// The <see cref="ILocalRouterContext"/> interface extends the <see cref="INavigationContext"/> interface
/// to provide additional properties specific to the local routing context, including references to the
/// root ViewModel, the local router, and the parent router.
/// </remarks>
public interface ILocalRouterContext : INavigationContext
{
    /// <summary>
    /// Gets the root ViewModel for the local router context.
    /// </summary>
    public object RootViewModel { get; }

    /// <summary>
    /// Gets the local router instance.
    /// </summary>
    public IRouter LocalRouter { get; }

    /// <summary>
    /// Gets the parent router instance.
    /// </summary>
    public IRouter ParentRouter { get; }
}
