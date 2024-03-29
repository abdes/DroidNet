// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

public class NavigationStart(string? url, NavigationOptions options) : NavigationEvent(options)
{
    public string? Url { get; } = url;

    public override string ToString() => $"Navigation Start -> {this.Url}";
}
