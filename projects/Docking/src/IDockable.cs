// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

/// <summary>
/// Specifies the interface of a dockable entity, i.e., something that can be embedded in a dock and docked with a docker.
/// </summary>
/// <remarks>
/// Implementing this interface allows an object to be managed within a docking framework, providing properties for identification,
/// display titles, preferred dimensions, and ownership. It also includes an event for disposal notification.
/// </remarks>
public interface IDockable : IDisposable
{
    /// <summary>
    /// Occurs when the dockable entity is disposed.
    /// </summary>
    public event EventHandler<EventArgs>? OnDisposed;

    /// <summary>
    /// Gets the unique identifier of the dockable entity.
    /// </summary>
    /// <value>
    /// A <see langword="string"/> representing the unique identifier of the dockable entity.
    /// </value>
    public string Id { get; }

    /// <summary>
    /// Gets or sets the title of the dockable entity.
    /// </summary>
    /// <value>
    /// A <see langword="string"/> representing the title of the dockable entity.
    /// </value>
    public string Title { get; set; }

    /// <summary>
    /// Gets or sets the minimized title of the dockable entity.
    /// </summary>
    /// <value>
    /// A <see langword="string"/> representing the minimized title of the dockable entity.
    /// </value>
    public string MinimizedTitle { get; set; }

    /// <summary>
    /// Gets or sets the tabbed title of the dockable entity.
    /// </summary>
    /// <value>
    /// A <see langword="string"/> representing the tabbed title of the dockable entity.
    /// </value>
    public string TabbedTitle { get; set; }

    /// <summary>
    /// Gets or sets the preferred width of the dockable entity.
    /// </summary>
    /// <value>
    /// A <see cref="Width"/> representing the preferred width of the dockable entity.
    /// </value>
    public Width PreferredWidth { get; set; }

    /// <summary>
    /// Gets or sets the preferred height of the dockable entity.
    /// </summary>
    /// <value>
    /// A <see cref="Height"/> representing the preferred height of the dockable entity.
    /// </value>
    public Height PreferredHeight { get; set; }

    /// <summary>
    /// Gets the view model associated with the dockable entity.
    /// </summary>
    /// <value>
    /// An <see langword="object"/> representing the view model associated with the dockable entity,
    /// or <see langword="null"/> if no view model is associated.
    /// </value>
    public object? ViewModel { get; }

    /// <summary>
    /// Gets the owner dock of the dockable entity.
    /// </summary>
    /// <value>
    /// An <see cref="IDock"/> representing the owner dock of the dockable entity, or <see langword="null"/>
    /// if the entity is not owned by any dock.
    /// </value>
    public IDock? Owner { get; }

    /// <summary>
    /// Gets a value indicating whether the dockable entity is currently active.
    /// </summary>
    /// <value>
    /// <see langword="true"/> if the dockable entity is active; otherwise, <see langword="false"/>.
    /// </value>
    public bool IsActive { get; }
}
