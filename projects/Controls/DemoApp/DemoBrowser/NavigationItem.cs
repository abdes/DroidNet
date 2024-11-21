// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DemoBrowser;

/// <summary>
/// Represents a navigation item used in the demo browser.
/// </summary>
/// <param name="path">The path associated with the navigation item.</param>
/// <param name="text">The display text for the navigation item.</param>
/// <param name="target">The target view model type for the navigation item.</param>
public class NavigationItem(string path, string text, Type target)
{
    /// <summary>
    /// Gets the path associated with the navigation item.
    /// </summary>
    public string Path { get; } = path;

    /// <summary>
    /// Gets the display text for the navigation item.
    /// </summary>
    public string Text { get; } = text;

    /// <summary>
    /// Gets the target view model type for the navigation item.
    /// </summary>
    public Type TargetViewModel { get; } = target;
}
