// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;

namespace DroidNet.Docking.Workspace;

/// <summary>
/// Represents a <see cref="LayoutSegment"/> that is also a container for <see cref="IDock"/> instances.
/// </summary>
/// <remarks>
/// <para>
/// The <see cref="DockGroup"/> class is designed to manage a collection of docks within a docking workspace. It provides methods
/// to add, insert, and remove docks, as well as to clear the entire collection of docks.
/// </para>
/// <para>
/// This class ensures that the docks are managed in a structured manner, allowing for flexible layout and organization within the workspace.
/// </para>
/// </remarks>
/// <example>
/// <para>
/// To create a new instance of a derived <see cref="DockGroup"/> class and add docks to it, use the following code:
/// </para>
/// <code><![CDATA[
/// var docker = new CustomDocker();
/// var dockGroup = new CustomDockGroup(docker, DockGroupOrientation.Horizontal);
/// var dock = new CustomDock();
/// dockGroup.AddDock(dock);
/// ]]></code>
/// </example>
public abstract class DockGroup : LayoutSegment, IDockGroup
{
    /// <summary>The collection of docks in this group.</summary>
    private readonly ObservableCollection<IDock> docks = [];

    /// <summary>
    /// Initializes a new instance of the <see cref="DockGroup"/> class.
    /// </summary>
    /// <param name="docker">The <see cref="IDocker"/> managing this group.</param>
    /// <param name="orientation">The orientation of the dock group.</param>
    protected DockGroup(IDocker docker, DockGroupOrientation orientation)
        : base(docker, orientation)
    {
        this.Docks = new ReadOnlyObservableCollection<IDock>(this.docks);
    }

    /// <summary>
    /// Gets a read-only collection of docks in this group that provides notifications when items get added or removed, or when
    /// the whole list is refreshed.
    /// </summary>
    public ReadOnlyObservableCollection<IDock> Docks { get; }

    /// <inheritdoc />
    public override string ToString()
    {
        var strDocks = string.Join(',', this.docks);
        return $"{base.ToString()} ({strDocks})";
    }

    /// <summary>
    /// Adds a dock to the end of this group's docks collection.
    /// </summary>
    /// <param name="dock">The dock to be added to the end of this group's docks collection.</param>
    /// <remarks>
    /// This method adds the specified dock to the end of the collection. It ensures that the dock is properly managed within the group.
    /// </remarks>
    internal virtual void AddDock(IDock dock) => this.docks.Add(dock);

    /// <summary>
    /// Adds a dock to this group's docks collection at a position determined by the specified <paramref name="anchor"/>.
    /// </summary>
    /// <param name="dock">The dock to be added to this group's docks collection.</param>
    /// <param name="anchor">The anchor specifying an existing dock and a relative position to that dock.</param>
    /// <exception cref="ArgumentException">
    /// Thrown when the specified <paramref name="anchor"/> does not specify a relative dock or when it specifies a relative position
    /// other than <see cref="AnchorPosition.Left"/>, <see cref="AnchorPosition.Right"/>, <see cref="AnchorPosition.Top"/>, or
    /// <see cref="AnchorPosition.Bottom"/>.
    /// </exception>
    /// <remarks>
    /// <para>
    /// A relative position of <see cref="AnchorPosition.Left"/> or <see cref="AnchorPosition.Top"/> will insert the new dock
    /// <em>before</em> its anchor dock, while a relative position of <see cref="AnchorPosition.Right"/> or <see cref="AnchorPosition.Bottom"/> will insert it after.
    /// </para>
    /// </remarks>
    internal abstract void AddDock(IDock dock, Anchor anchor);

    /// <summary>
    /// Inserts a dock into this group's docks collection at the specified index.
    /// </summary>
    /// <param name="index">The zero-based index at which <paramref name="dock"/> should be inserted.</param>
    /// <param name="dock">The dock to insert.</param>
    /// <exception cref="ArgumentOutOfRangeException">
    /// Thrown when <paramref name="index"/> is less than zero or greater than the number of docks in the group.
    /// </exception>
    /// <remarks>
    /// If <paramref name="index"/> is equal to the number of docks in the group, <paramref name="dock"/> is added to the end of
    /// the docks' collection.
    /// </remarks>
    internal virtual void InsertDock(int index, IDock dock) => this.docks.Insert(index, dock);

    /// <summary>
    /// Removes the first occurrence of the specified dock from this group's docks collection.
    /// </summary>
    /// <param name="dock">The dock to remove.</param>
    /// <returns>
    /// <see langword="true"/> if <paramref name="dock"/> is successfully removed; otherwise, <see langword="false"/>.
    /// This method also returns <see langword="false"/> if the item was not found in this group's docks collection.
    /// </returns>
    /// <remarks>
    /// This method removes the specified dock from the collection. If the dock is not found, it returns <see langword="false"/>.
    /// </remarks>
    internal virtual bool RemoveDock(IDock dock) => this.docks.Remove(dock);

    /// <summary>
    /// Removes all docks from this group.
    /// </summary>
    /// <remarks>
    /// This base method implementation does not dispose of the removed docks and does not change the <see cref="IDockGroup"/>
    /// association. Derived classes should explicitly specify and implement the appropriate behavior.
    /// </remarks>
    protected virtual void ClearDocks() => this.docks.Clear();
}
