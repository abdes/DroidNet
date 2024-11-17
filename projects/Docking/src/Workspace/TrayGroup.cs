// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

/// <summary>
/// Represents a tray group within the docking workspace.
/// </summary>
/// <remarks>
/// The <see cref="TrayGroup"/> class is a specialized type of <see cref="DockGroup"/> that is used to manage tray segments within the docking workspace.
/// The orientation of a <see cref="TrayGroup"/> can only be set at creation and cannot be changed afterwards.
/// </remarks>
public sealed class TrayGroup : DockGroup
{
    private readonly AnchorPosition position;

    /// <summary>
    /// Initializes a new instance of the <see cref="TrayGroup"/> class with the specified docker and anchor position.
    /// </summary>
    /// <param name="docker">The docker that manages this tray group.</param>
    /// <param name="position">The anchor position of the tray group.</param>
    /// <exception cref="ArgumentException">Thrown when the <paramref name="position"/> is <see cref="AnchorPosition.With"/> or <see cref="AnchorPosition.Center"/>.</exception>
    /// <remarks>
    /// This constructor sets the docker and anchor position for the tray group. The orientation is determined based on the anchor position.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// IDocker docker = ...;
    /// var trayGroup = new TrayGroup(docker, AnchorPosition.Left);
    /// ]]></code>
    /// </para>
    /// </remarks>
    public TrayGroup(IDocker docker, AnchorPosition position)
        : base(
            docker,
            position is AnchorPosition.Left or AnchorPosition.Right ? DockGroupOrientation.Vertical : DockGroupOrientation.Horizontal)
    {
        if (position is AnchorPosition.With or AnchorPosition.Center)
        {
            throw new ArgumentException($"cannot use {position} for a {nameof(TrayGroup)}", nameof(position));
        }

        this.position = position;
    }

    /// <summary>
    /// Gets the orientation of the tray group.
    /// </summary>
    /// <value>
    /// A <see cref="DockGroupOrientation"/> value representing the orientation of the tray group.
    /// </value>
    /// <exception cref="InvalidOperationException">Thrown when attempting to set the orientation after creation.</exception>
    /// <remarks>
    /// The orientation of a <see cref="TrayGroup"/> can only be set at creation and cannot be changed afterwards.
    /// </remarks>
    public override DockGroupOrientation Orientation
    {
        get => base.Orientation;
        internal set => throw new InvalidOperationException(
            $"orientation of an {nameof(TrayGroup)} can only be set at creation");
    }

    /// <summary>
    /// Returns a string that represents the current object.
    /// </summary>
    /// <returns>A string that represents the current object.</returns>
    /// <remarks>
    /// This method overrides the <see cref="object.ToString"/> method to provide a string representation of the tray group.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// var trayGroup = new TrayGroup(docker, AnchorPosition.Left);
    /// Console.WriteLine(trayGroup.ToString());
    /// ]]></code>
    /// </para>
    /// </remarks>
    public override string ToString() => $"{this.position} TrayGroup {base.ToString()}";

    /// <summary>
    /// Adds a dock to the tray group with the specified anchor.
    /// </summary>
    /// <param name="dock">The dock to add.</param>
    /// <param name="anchor">The anchor specifying the position of the dock.</param>
    /// <exception cref="InvalidOperationException">Thrown when attempting to add a dock with an anchor in a tray group.</exception>
    /// <remarks>
    /// This method is not supported for tray groups and will always throw an <see cref="InvalidOperationException"/>.
    /// </remarks>
    internal override void AddDock(IDock dock, Anchor anchor)
        => throw new InvalidOperationException("cannot add a dock with an anchor in a tray group");
}
