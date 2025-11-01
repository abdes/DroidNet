// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Service for managing a floating overlay during tab drag operations. At most one session may
///     exist per application process. All methods must be called from the application's UI thread.
/// </summary>
public interface IDragVisualService
{
    /// <summary>
    ///     Starts a drag visual session using the provided descriptor. Returns an opaque token that
    ///     must be provided to <see cref="EndSession(DragSessionToken)"/>.
    /// </summary>
    /// <param name="descriptor">Descriptor describing visual content and state.</param>
    /// <param name="hotspot">Hotspot offset from the top-left of the overlay where the cursor should align (in logical pixels).</param>
    /// <returns>A token representing the active session.</returns>
    /// <exception cref="InvalidOperationException">If a session is already active.</exception>
    public DragSessionToken StartSession(DragVisualDescriptor descriptor, Windows.Foundation.Point hotspot);

    /// <summary>
    ///     Updates the position of the drag overlay identified by <paramref name="token"/> to the
    ///     specified screen coordinates.
    /// </summary>
    /// <param name="token">Session token.</param>
    /// <param name="screenPoint">Screen coordinates in logical pixels where the overlay should be
    /// positioned.</param>
    public void UpdatePosition(DragSessionToken token, Windows.Foundation.Point screenPoint);

    /// <summary>
    ///     Ends the previously started session identified by <paramref name="token"/>.
    /// </summary>
    /// <param name="token">Token returned from <see cref="StartSession(DragVisualDescriptor, Windows.Foundation.Point)"/>.</param>
    public void EndSession(DragSessionToken token);

    /// <summary>
    ///     Gets the descriptor associated with <paramref name="token"/>, or <see langword="null"/>
    ///     token is unknown or the session ended.
    /// </summary>
    /// <param name="token">Session token.</param>
    /// <returns>The session's descriptor or <see langword="null"/>.</returns>
    public DragVisualDescriptor? GetDescriptor(DragSessionToken token);
}
