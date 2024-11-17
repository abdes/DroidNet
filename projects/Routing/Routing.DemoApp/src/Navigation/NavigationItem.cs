// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Demo.Navigation;

/// <summary>
/// Represents a navigation item used in the routing system.
/// </summary>
/// <param name="path">The navigation path associated with this item.</param>
/// <param name="text">The display text for this navigation item.</param>
/// <param name="icon">The icon associated with this navigation item.</param>
/// <param name="accessKey">The access key for keyboard shortcuts.</param>
/// <param name="targetViewmodel">The type of the target view model.</param>
public class NavigationItem(string path, string text, string icon, string accessKey, Type targetViewmodel)
{
    /// <summary>
    /// Gets the navigation path associated with this item.
    /// </summary>
    public string Path { get; } = path;

    /// <summary>
    /// Gets the display text for this navigation item.
    /// </summary>
    public string Text { get; } = text;

    /// <summary>
    /// Gets the icon associated with this navigation item.
    /// </summary>
    public string Icon { get; } = icon;

    /// <summary>
    /// Gets the access key for keyboard shortcuts.
    /// </summary>
    public string AccessKey { get; } = accessKey;

    /// <summary>
    /// Gets the type of the target view model.
    /// </summary>
    public Type TargetViewModel { get; } = targetViewmodel;
}
