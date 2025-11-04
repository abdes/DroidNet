// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Aura.Decoration;
using DroidNet.Controls.Menus;
using Microsoft.UI;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Windowing;

/// <summary>
///     Encapsulates metadata and state information for a managed window.
/// </summary>
/// <remarks>
///     When decoration specifies a menu via <see cref="Decoration.WindowDecorationOptions.Menu"/>,
///     the WindowContext will resolve the menu provider from the service provider during creation
///     and store the resulting <see cref="IMenuSource"/> for the lifetime of the window.
/// <para>
///     Menu sources are lightweight data structures that do not require explicit disposal. They
///     will be garbage collected when the WindowContext is no longer referenced.
/// </para>
/// </remarks>
public sealed partial class WindowContext : ObservableObject
{
    private IMenuSource? menuSource;

    /// <summary>
    ///     Gets the unique identifier for the window.
    /// </summary>
    public required WindowId Id { get; init; }

    /// <summary>
    ///     Gets the WinUI Window instance.
    /// </summary>
    public required Window Window { get; init; }

    /// <summary>
    ///     Gets a value indicating whether the window is currently active.
    /// </summary>
    public bool IsActive { get; private set; }

    /// <summary>
    ///     Gets the category of the window.
    /// </summary>
    public required WindowCategory? Category { get; init; }

    /// <summary>
    ///     Gets the optional decoration options for the window.
    /// </summary>
    [ObservableProperty]
    public partial WindowDecorationOptions? Decorations { get; set; }

    /// <summary>
    ///     Gets the menu source for this window, if one was created from a menu provider.
    /// </summary>
    /// <remarks>
    ///     This property returns the menu source that was created during window initialization
    ///     based on the decoration's menu options. Returns null if no menu was specified or if the
    ///     menu provider could not be found.
    /// </remarks>
    public IMenuSource? MenuSource => this.menuSource;

    /// <summary>
    ///     Gets the optional metadata for custom window properties.
    /// </summary>
    public IReadOnlyDictionary<string, object>? Metadata { get; init; }

    /// <summary>
    ///     Gets the timestamp when the window was created.
    /// </summary>
    public required DateTimeOffset CreatedAt { get; init; }

    /// <summary>
    ///     Gets the timestamp of the most recent activation.
    /// </summary>
    public DateTimeOffset? LastActivatedAt { get; private set; }

    /// <summary>
    ///     Updates the activation state of this window context.
    /// </summary>
    /// <param name="isActive">Whether the window is active.</param>
    /// <returns>A new <see cref="WindowContext"/> with updated activation state.</returns>
    public WindowContext WithActivationState(bool isActive)
    {
        this.IsActive = isActive;
        this.LastActivatedAt = isActive ? DateTimeOffset.UtcNow : this.LastActivatedAt;
        return this;
    }

    /// <summary>
    ///     Sets the menu source for this window context.
    /// </summary>
    /// <param name="menuSource">The menu source to set.</param>
    /// <remarks>
    ///     This method is intended to be called by <see cref="IWindowContextFactory"/>
    ///     implementations during context initialization.
    /// </remarks>
    internal void SetMenuSource(IMenuSource menuSource)
    {
        ArgumentNullException.ThrowIfNull(menuSource);
        this.menuSource = menuSource;
    }
}
