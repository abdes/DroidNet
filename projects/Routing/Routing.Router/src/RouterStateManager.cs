// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Simple implementation for <see cref="IRouterStateManager" /> that can only
/// remember the current navigation.
/// </summary>
public class RouterStateManager() : IRouterStateManager
{
    /// <inheritdoc/>
    public void ToDo() => throw new InvalidOperationException();
}
