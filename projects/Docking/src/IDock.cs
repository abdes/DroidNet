// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;

namespace DroidNet.Docking;

/// <summary>
/// Represents a dock that can contain multiple dockable entities and manage their layout and state.
/// </summary>
/// <remarks>
/// The <see cref="IDock"/> interface provides methods and properties to manage dockable entities within a docking framework.
/// It supports operations such as adopting, disowning, and destroying dockables, as well as migrating dockables between docks.
/// </remarks>
public interface IDock : IDisposable
{
    /// <summary>
    /// Gets the unique identifier of the dock.
    /// </summary>
    /// <value>
    /// A <see cref="DockId"/> representing the unique identifier of the dock.
    /// </value>
    DockId Id { get; }

    /// <summary>
    /// Gets a read-only collection of dockable entities contained within the dock.
    /// </summary>
    /// <value>
    /// A <see cref="ReadOnlyObservableCollection{T}"/> of <see cref="IDockable"/> representing the dockable entities.
    /// </value>
    ReadOnlyObservableCollection<IDockable> Dockables { get; }

    /// <summary>
    /// Gets the currently active dockable entity within the dock.
    /// </summary>
    /// <value>
    /// An <see cref="IDockable"/> representing the currently active dockable entity, or <see langword="null"/> if no dockable is active.
    /// </value>
    IDockable? ActiveDockable { get; }

    /// <summary>
    /// Gets the current docking state of the dock.
    /// </summary>
    /// <value>
    /// A <see cref="DockingState"/> representing the current docking state of the dock.
    /// </value>
    DockingState State { get; }

    /// <summary>
    /// Gets a value indicating whether the dock can be minimized.
    /// </summary>
    /// <value>
    /// <see langword="true"/> if the dock can be minimized; otherwise, <see langword="false"/>.
    /// </value>
    bool CanMinimize { get; }

    /// <summary>
    /// Gets a value indicating whether the dock can be closed.
    /// </summary>
    /// <value>
    /// <see langword="true"/> if the dock can be closed; otherwise, <see langword="false"/>.
    /// </value>
    bool CanClose { get; }

    /// <summary>
    /// Gets the anchor point of the dock within the workspace.
    /// </summary>
    /// <value>
    /// An <see cref="Anchor"/> representing the anchor point of the dock, or <see langword="null"/> if the dock is not anchored.
    /// </value>
    Anchor? Anchor { get; }

    /// <summary>
    /// Gets the width of the dock.
    /// </summary>
    /// <value>
    /// A <see cref="Width"/> representing the width of the dock.
    /// </value>
    Width Width { get; }

    /// <summary>
    /// Gets the height of the dock.
    /// </summary>
    /// <value>
    /// A <see cref="Height"/> representing the height of the dock.
    /// </value>
    Height Height { get; }

    /// <summary>
    /// Gets the docker that manages this dock.
    /// </summary>
    /// <value>
    /// An <see cref="IDocker"/> representing the docker that manages this dock, or <see langword="null"/> if the dock is not managed by any docker.
    /// </value>
    IDocker? Docker { get; }

    /// <summary>
    /// Adopts a dockable entity into the dock at the specified position.
    /// </summary>
    /// <param name="dockable">The dockable entity to adopt.</param>
    /// <param name="position">The position at which to place the dockable entity. The default is <see cref="DockablePlacement.Last"/>.</param>
    /// <exception cref="ArgumentNullException">Thrown when <paramref name="dockable"/> is <see langword="null"/>.</exception>
    /// <example>
    /// <code><![CDATA[
    /// IDock dock = ...;
    /// IDockable dockable = ...;
    /// dock.AdoptDockable(dockable, DockablePlacement.First);
    /// ]]></code>
    /// </example>
    void AdoptDockable(IDockable dockable, DockablePlacement position = DockablePlacement.Last);

    /// <summary>
    /// Disowns a dockable entity from the dock.
    /// </summary>
    /// <param name="dockable">The dockable entity to disown.</param>
    /// <exception cref="ArgumentNullException">Thrown when <paramref name="dockable"/> is <see langword="null"/>.</exception>
    /// <example>
    /// <code><![CDATA[
    /// IDock dock = ...;
    /// IDockable dockable = ...;
    /// dock.DisownDockable(dockable);
    /// ]]></code>
    /// </example>
    void DisownDockable(IDockable dockable);

    /// <summary>
    /// Destroys a dockable entity within the dock.
    /// </summary>
    /// <param name="dockable">The dockable entity to destroy.</param>
    /// <exception cref="ArgumentNullException">Thrown when <paramref name="dockable"/> is <see langword="null"/>.</exception>
    /// <example>
    /// <code><![CDATA[
    /// IDock dock = ...;
    /// IDockable dockable = ...;
    /// dock.DestroyDockable(dockable);
    /// ]]></code>
    /// </example>
    void DestroyDockable(IDockable dockable);

    /// <summary>
    /// Migrates all dockable entities from this dock to another dock.
    /// </summary>
    /// <param name="destinationDock">The destination dock to which the dockable entities will be migrated.</param>
    /// <exception cref="ArgumentNullException">Thrown when <paramref name="destinationDock"/> is <see langword="null"/>.</exception>
    /// <example>
    /// <code><![CDATA[
    /// IDock sourceDock = ...;
    /// IDock destinationDock = ...;
    /// sourceDock.MigrateDockables(destinationDock);
    /// ]]></code>
    /// </example>
    void MigrateDockables(IDock destinationDock);
}
