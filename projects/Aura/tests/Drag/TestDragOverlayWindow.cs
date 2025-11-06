// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Generic;
using DroidNet.Aura.Drag;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Aura.Tests.Drag;

/// <summary>
///     Test-specific implementation of <see cref="DragOverlayWindow"/> that tracks method calls
///     without requiring actual UI rendering.
/// </summary>
/// <remarks>
///     This class overrides virtual methods to be no-ops and records all <see cref="MoveWindow"/>
///     calls for test verification purposes.
/// </remarks>
internal sealed partial class TestDragOverlayWindow : DragOverlayWindow
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="TestDragOverlayWindow"/> class.
    /// </summary>
    public TestDragOverlayWindow()
    {
        // Set Content to a Grid for the service to use
        this.Content = new Grid();
    }

    /// <summary>
    ///     Gets the history of all <see cref="MoveWindow"/> calls made during testing.
    /// </summary>
    public List<Windows.Graphics.PointInt32> MoveCallHistory { get; } = [];

    /// <inheritdoc/>
    public override void ShowNoActivate()
    {
        // No-op in tests - window activation is not needed for unit tests
    }

    /// <inheritdoc/>
    public override void SetSize(Windows.Foundation.Size size)
    {
        // No-op in tests - window sizing is not needed for unit tests
    }

    /// <inheritdoc/>
    public override void MoveWindow(Windows.Graphics.PointInt32 position)
    {
        this.MoveCallHistory.Add(position);
    }
}
