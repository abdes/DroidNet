// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Events;

public class NavigationEvent : RouterEvent
{
}

public class NavigationStart(string? url) : NavigationEvent
{
    public string? Url { get; } = url;

    public override string ToString() => $"Navigation Start -> {this.Url}";
}

public class NavigationEnd(string? url) : NavigationEvent
{
    public string? Url { get; } = url;

    public override string ToString() => $"Navigation End -> {this.Url}";
}

public class NavigationError : NavigationEvent;
