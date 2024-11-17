// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking.Workspace;

namespace DroidNet.Docking;

/// <summary>
/// Represents an abstract base class for layout engines that manage the layout of dockable entities within a workspace.
/// </summary>
/// <remarks>
/// The <see cref="LayoutEngine"/> class provides methods to start and end layouts, place docks and trays, and manage layout flows.
/// Derived classes should implement the abstract methods to define specific layout behaviors.
/// </remarks>
public abstract class LayoutEngine
{
    private readonly Stack<LayoutFlow> flows = new();

    /// <summary>
    /// Gets the current layout flow.
    /// </summary>
    /// <value>The current <see cref="LayoutFlow"/>.</value>
    public LayoutFlow CurrentFlow => this.flows.Peek();

    /// <summary>
    /// Starts the layout process for the specified layout segment.
    /// </summary>
    /// <param name="segment">The layout segment to start the layout for.</param>
    /// <returns>A <see cref="LayoutFlow"/> representing the started layout flow.</returns>
    /// <remarks>
    /// This method initializes the layout process for a given segment. Derived classes should provide the specific implementation.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// LayoutEngine engine = new CustomLayoutEngine();
    /// ILayoutSegment segment = ...;
    /// LayoutFlow flow = engine.StartLayout(segment);
    /// ]]></code>
    /// </para>
    /// </remarks>
    public abstract LayoutFlow StartLayout(ILayoutSegment segment);

    /// <summary>
    /// Places a dock within the current layout.
    /// </summary>
    /// <param name="dock">The dock to place.</param>
    /// <remarks>
    /// This method places a dock within the current layout flow. Derived classes should provide the specific implementation.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// LayoutEngine engine = new CustomLayoutEngine();
    /// IDock dock = ...;
    /// engine.PlaceDock(dock);
    /// ]]></code>
    /// </para>
    /// </remarks>
    public abstract void PlaceDock(IDock dock);

    /// <summary>
    /// Places a tray within the current layout.
    /// </summary>
    /// <param name="tray">The tray to place.</param>
    /// <remarks>
    /// This method places a tray within the current layout flow. Derived classes should provide the specific implementation.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// LayoutEngine engine = new CustomLayoutEngine();
    /// TrayGroup tray = ...;
    /// engine.PlaceTray(tray);
    /// ]]></code>
    /// </para>
    /// </remarks>
    public abstract void PlaceTray(TrayGroup tray);

    /// <summary>
    /// Starts a new layout flow for the specified layout segment.
    /// </summary>
    /// <param name="segment">The layout segment to start the flow for.</param>
    /// <returns>A <see cref="LayoutFlow"/> representing the started layout flow.</returns>
    /// <remarks>
    /// This method initializes a new layout flow for a given segment. Derived classes should provide the specific implementation.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// LayoutEngine engine = new CustomLayoutEngine();
    /// ILayoutSegment segment = ...;
    /// LayoutFlow flow = engine.StartFlow(segment);
    /// ]]></code>
    /// </para>
    /// </remarks>
    public abstract LayoutFlow StartFlow(ILayoutSegment segment);

    /// <summary>
    /// Ends the current layout flow.
    /// </summary>
    /// <remarks>
    /// This method finalizes the current layout flow. Derived classes should provide the specific implementation.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// LayoutEngine engine = new CustomLayoutEngine();
    /// engine.EndFlow();
    /// ]]></code>
    /// </para>
    /// </remarks>
    public abstract void EndFlow();

    /// <summary>
    /// Ends the layout process.
    /// </summary>
    /// <remarks>
    /// This method finalizes the entire layout process. Derived classes should provide the specific implementation.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// LayoutEngine engine = new CustomLayoutEngine();
    /// engine.EndLayout();
    /// ]]></code>
    /// </para>
    /// </remarks>
    public abstract void EndLayout();

    /// <summary>
    /// Pushes a new layout flow onto the stack.
    /// </summary>
    /// <param name="state">The layout flow to push onto the stack.</param>
    /// <remarks>
    /// This method is used internally to manage the stack of layout flows.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// LayoutEngine engine = new CustomLayoutEngine();
    /// LayoutFlow flow = ...;
    /// engine.PushFlow(flow);
    /// ]]></code>
    /// </para>
    /// </remarks>
    internal void PushFlow(LayoutFlow state) => this.flows.Push(state); /* $"==> {state}"*/

    /// <summary>
    /// Pops the current layout flow from the stack.
    /// </summary>
    /// <remarks>
    /// This method is used internally to manage the stack of layout flows.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// LayoutEngine engine = new CustomLayoutEngine();
    /// engine.PopFlow();
    /// ]]></code>
    /// </para>
    /// </remarks>
    internal void PopFlow()
        => _ = this.flows.Pop(); /* $"<== {(this.flows.Count != 0 ? this.flows.Peek() : string.Empty)}" */
}
