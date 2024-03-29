// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

public class RoutesRecognized(IUrlTree urlTree) : RouterEvent
{
    public IUrlTree UrlTree { get; } = urlTree;

    /// <inheritdoc />
    public override string ToString() => $"Routes recognized -> {this.UrlTree}";
}
