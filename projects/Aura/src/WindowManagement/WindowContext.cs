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
/// <param name="WindowType">The type of window (e.g., "Main", "Tool", "Dialog", "Document").</param>
/// <param name="Title">The window title.</param>
/// <param name="CreatedAt">Timestamp when the window was created.</param>
/// <param name="Metadata">Optional metadata for custom window properties.</param>
/// <param name="IsActive">Indicates whether the window is currently active.</param>
/// <param name="LastActivatedAt">The timestamp of the most recent activation.</param>
public sealed record WindowContext(
    Guid Id,
    Window Window,
    string WindowType,
    string Title,
    DateTimeOffset CreatedAt,
    IReadOnlyDictionary<string, object>? Metadata = null,
    bool IsActive = false,
    DateTimeOffset? LastActivatedAt = null)
{
    /// <summary>
    /// Creates a new <see cref="WindowContext"/> for a given window.
    /// </summary>
    /// <param name="window">The window to wrap.</param>
    /// <param name="windowType">The type of window.</param>
    /// <param name="title">The window title.</param>
    /// <param name="metadata">Optional metadata.</param>
    /// <returns>A new <see cref="WindowContext"/> instance.</returns>
    public static WindowContext Create(
        Window window,
        string windowType = "Main",
        string? title = null,
        IReadOnlyDictionary<string, object>? metadata = null)
    {
        ArgumentNullException.ThrowIfNull(window);
        ArgumentException.ThrowIfNullOrWhiteSpace(windowType);

        return new WindowContext(
            Id: Guid.NewGuid(),
            Window: window,
            WindowType: windowType,
            Title: title ?? window.Title ?? "Untitled Window",
            CreatedAt: DateTimeOffset.UtcNow,
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
