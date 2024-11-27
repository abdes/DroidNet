// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>
/// Represents a navigation item in the project browser.
/// </summary>
/// <param name="path">The path associated with the navigation item.</param>
/// <param name="text">The display text for the navigation item.</param>
/// <param name="icon">The icon representing the navigation item.</param>
/// <param name="accessKey">The access key for the navigation item.</param>
/// <param name="target">The target view model type for the navigation item.</param>
public class NavigationItem(string path, string text, string icon, string accessKey, Type target)
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
    /// Gets the icon representing the navigation item.
    /// </summary>
    public string Icon { get; } = icon;

    /// <summary>
    /// Gets the access key for the navigation item.
    /// </summary>
    public string AccessKey { get; } = accessKey;

    /// <summary>
    /// Gets the target view model type for the navigation item.
    /// </summary>
    public Type TargetViewModel { get; } = target;
}
