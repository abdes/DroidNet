// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Controls.Tabs.Tests;

/// <summary>
/// Mock implementation of IDragVisualService for testing purposes.
/// Tracks method calls and arguments to verify coordinator behavior.
/// </summary>
[ExcludeFromCodeCoverage]
internal sealed class MockDragVisualService : IDragVisualService
{
    private readonly List<Windows.Foundation.Point> updatePositionCalls = [];

    /// <summary>
    /// Gets the list of screen points passed to UpdatePosition calls.
    /// These should all be in physical pixels (as returned by GetCursorPos).
    /// </summary>
    public IReadOnlyList<Windows.Foundation.Point> UpdatePositionCalls => this.updatePositionCalls;

    /// <summary>
    /// Gets the hotspot passed to StartSession (should be in logical pixels).
    /// </summary>
    public Windows.Foundation.Point? StartSessionHotspot { get; private set; }

    /// <inheritdoc/>
    public DragSessionToken StartSession(DragVisualDescriptor descriptor, Windows.Foundation.Point hotspot)
    {
        this.StartSessionHotspot = hotspot;
        return new DragSessionToken { Id = Guid.NewGuid() };
    }

    /// <inheritdoc/>
    public void UpdatePosition(DragSessionToken token, Windows.Foundation.Point screenPoint)
        => this.updatePositionCalls.Add(screenPoint); // Track all position updates - these should be physical pixels

    /// <inheritdoc/>
    public void EndSession(DragSessionToken token)
    {
        // Mock implementation - no-op
    }

    /// <inheritdoc/>
    public DragVisualDescriptor? GetDescriptor(DragSessionToken token)
        => null; // Mock implementation

    /// <summary>
    /// Resets call tracking for test isolation.
    /// </summary>
    public void Reset()
    {
        this.updatePositionCalls.Clear();
        this.StartSessionHotspot = null;
    }
}
