// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Aura.WindowManagement;

/// <summary>
/// Encapsulates metadata and state information for a managed window.
/// </summary>
/// <param name="Id">Unique identifier for the window.</param>
/// <param name="Window">The WinUI Window instance.</param>
/// <param name="Category">The category of the window. Use constants from <see cref="WindowCategory"/>.</param>
/// <param name="Title">The window title.</param>
/// <param name="CreatedAt">Timestamp when the window was created.</param>
/// <param name="Decoration">Optional decoration options for the window. Null means no decoration was specified.</param>
/// <param name="Metadata">Optional metadata for custom window properties.</param>
/// <param name="IsActive">Indicates whether the window is currently active.</param>
/// <param name="LastActivatedAt">The timestamp of the most recent activation.</param>
public sealed record WindowContext(
    Guid Id,
    Window Window,
    string Category,
    string Title,
    DateTimeOffset CreatedAt,
    Decoration.WindowDecorationOptions? Decoration = null,
    IReadOnlyDictionary<string, object>? Metadata = null,
    bool IsActive = false,
    DateTimeOffset? LastActivatedAt = null)
{
    /// <summary>
    /// Creates a new <see cref="WindowContext"/> for a given window.
    /// </summary>
    /// <param name="window">The window to wrap.</param>
    /// <param name="category">The category of the window. Use constants from <see cref="WindowCategory"/>.</param>
    /// <param name="title">The window title.</param>
    /// <param name="decoration">Optional decoration options for the window.</param>
    /// <param name="metadata">Optional metadata.</param>
    /// <returns>A new <see cref="WindowContext"/> instance.</returns>
    public static WindowContext Create(
        Window window,
        string category,
        string? title = null,
        Decoration.WindowDecorationOptions? decoration = null,
        IReadOnlyDictionary<string, object>? metadata = null)
    {
        ArgumentNullException.ThrowIfNull(window);
        ArgumentException.ThrowIfNullOrWhiteSpace(category);

        return new WindowContext(
            Id: Guid.NewGuid(),
            Window: window,
            Category: category,
            Title: title ?? window.Title ?? $"Untitled {category} Window",
            CreatedAt: DateTimeOffset.UtcNow,
            Decoration: decoration,
            Metadata: metadata);
    }

    /// <summary>
    /// Creates a copy of this context with updated activation state.
    /// </summary>
    /// <param name="isActive">Whether the window is active.</param>
    /// <returns>A new <see cref="WindowContext"/> with updated state.</returns>
    public WindowContext WithActivationState(bool isActive)
    {
        var activationTimestamp = isActive ? DateTimeOffset.UtcNow : this.LastActivatedAt;
        return this with
        {
            IsActive = isActive,
            LastActivatedAt = activationTimestamp,
        };
    }
}
