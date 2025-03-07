// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

/// <summary>
/// Represents a docker that manages the docking operations and layout of dockable entities within a workspace.
/// </summary>
/// <remarks>
/// The <see cref="IDocker"/> interface provides methods to manage the lifecycle and layout of docks, including docking, floating,
/// minimizing, and resizing operations. It also includes an event to notify when the layout changes.
/// </remarks>
public interface IDocker : IDisposable
{
    /// <summary>
    /// Occurs when the layout of the docker changes.
    /// </summary>
    public event EventHandler<LayoutChangedEventArgs> LayoutChanged;

    /// <summary>
    /// Closes the specified dock.
    /// </summary>
    /// <param name="dock">The dock to close.</param>
    public void CloseDock(IDock dock);

    /// <summary>
    /// Docks the specified dock at the given anchor point.
    /// </summary>
    /// <param name="dock">The dock to dock.</param>
    /// <param name="anchor">The anchor point where the dock will be docked.</param>
    /// <param name="minimized">A value indicating whether the dock should be minimized. The default is <see langword="false"/>.</param>
    public void Dock(IDock dock, Anchor anchor, bool minimized = false);

    /// <summary>
    /// Floats the specified dock, making it a floating dock.
    /// </summary>
    /// <param name="dock">The dock to float.</param>
    public void FloatDock(IDock dock);

    /// <summary>
    /// Minimizes the specified dock.
    /// </summary>
    /// <param name="dock">The dock to minimize.</param>
    public void MinimizeDock(IDock dock);

    /// <summary>
    /// Pins the specified dock, making it always visible.
    /// </summary>
    /// <param name="dock">The dock to pin.</param>
    public void PinDock(IDock dock);

    /// <summary>
    /// Resizes the specified dock to the given width and height.
    /// </summary>
    /// <param name="dock">The dock to resize.</param>
    /// <param name="width">The new width of the dock. If <see langword="null"/>, the width remains unchanged.</param>
    /// <param name="height">The new height of the dock. If <see langword="null"/>, the height remains unchanged.</param>
    public void ResizeDock(IDock dock, Width? width, Height? height);

    /// <summary>
    /// Dumps the current state of the workspace for debugging purposes.
    /// </summary>
    /// <example>
    /// <code><![CDATA[
    /// IDocker docker = ...;
    /// docker.DumpWorkspace();
    /// ]]></code>
    /// </example>
    public void DumpWorkspace();

    /// <summary>
    /// Applies the specified layout engine to the docker.
    /// </summary>
    /// <param name="layoutEngine">The layout engine to apply.</param>
    public void Layout(LayoutEngine layoutEngine);
}

/// <summary>
/// Provides data for the <see cref="IDocker.LayoutChanged"/> event.
/// </summary>
/// <param name="reason">The reason for the layout change.</param>
public class LayoutChangedEventArgs(LayoutChangeReason reason) : EventArgs
{
    /// <summary>
    /// Gets the reason for the layout change.
    /// </summary>
    public LayoutChangeReason Reason => reason;
}
