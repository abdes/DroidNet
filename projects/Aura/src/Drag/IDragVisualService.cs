// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Coordinates;

namespace DroidNet.Aura.Drag;

/// <summary>
///     Service for managing a floating overlay during tab drag operations. At most one session may
///     exist per application process. All methods must be called from the application's UI thread.
/// </summary>
public interface IDragVisualService : IDisposable
{
    /// <summary>
    ///     Starts a drag visual session using the provided descriptor. Returns an opaque token that
    ///     must be provided to <see cref="EndSession(DragSessionToken)"/>.
    /// </summary>
    /// <param name="descriptor">Descriptor describing visual content and state.</param>
    /// <param name="initialPosition">Initial position in physical screen space.</param>
    /// <param name="hotspotOffsets">
    ///     Hotspot offset in **LOGICAL PIXELS** (XAML/DIP coordinate space) from the top-left of
    ///     the header image to align under the pointer. Service stores this in logical pixels and
    ///     converts to physical on every position update using the current monitor DPI.
    /// </param>
    /// <returns>A token representing the active session.</returns>
    /// <exception cref="InvalidOperationException">If a session is already active.</exception>
    public DragSessionToken StartSession(
        DragVisualDescriptor descriptor,
        SpatialPoint<PhysicalScreenSpace> initialPosition,
        SpatialPoint<ScreenSpace> hotspotOffsets);

    /// <summary>
    ///     Updates the position of the drag overlay identified by <paramref name="token"/> to the
    ///     specified screen coordinates. Service performs all DPI conversions internally based on
    ///     the current monitor DPI detected via <c>GetDpiForPhysicalPoint(physicalScreenPoint)</c>,
    ///     ensuring correct behavior when the cursor crosses monitors with different DPI scaling.
    /// </summary>
    /// <param name="token">Session token.</param>
    /// <param name="position">
    ///     Cursor location in **PHYSICAL SCREEN PIXELS** as a typed spatial point. Service converts
    ///     to logical pixels using current monitor DPI, subtracts the logical
    ///     windowPositionOffsets, and converts back to physical pixels for window positioning.
    /// </param>
    public void UpdatePosition(DragSessionToken token, SpatialPoint<PhysicalScreenSpace> position);

    /// <summary>
    ///     Ends the previously started session identified by <paramref name="token"/>.
    /// </summary>
    /// <param name="token">
    ///     Token returned from <see cref="StartSession(DragVisualDescriptor, SpatialPoint{PhysicalScreenSpace}, SpatialPoint{ScreenSpace})"/>.
    /// </param>
    public void EndSession(DragSessionToken token);

    /// <summary>
    ///     Gets the descriptor associated with <paramref name="token"/>, or <see langword="null"/>
    ///     token is unknown or the session ended.
    /// </summary>
    /// <param name="token">Session token.</param>
    /// <returns>The session's descriptor or <see langword="null"/>.</returns>
    public DragVisualDescriptor? GetDescriptor(DragSessionToken token);
}
