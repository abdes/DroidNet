// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking.Workspace;

namespace DroidNet.Docking.Detail;

/// <summary>
/// Provides extension methods for internal implementations of docking components.
/// </summary>
/// <remarks>
/// These extension methods are designed to cast interfaces to their concrete implementations.
/// They throw detailed exceptions if the cast fails, ensuring that only valid objects are processed.
/// </remarks>
internal static class InternalImplExtensions
{
    /// <summary>
    /// Casts an <see cref="IDockable"/> to its concrete <see cref="Dockable"/> implementation.
    /// </summary>
    /// <param name="dockable">The <see cref="IDockable"/> to cast.</param>
    /// <returns>The concrete <see cref="Dockable"/> implementation.</returns>
    /// <exception cref="ArgumentException">
    /// Thrown when the <paramref name="dockable"/> is not of type <see cref="Dockable"/>.
    /// </exception>
    /// <remarks>
    /// <para>
    /// This method ensures that the provided <see cref="IDockable"/> is of the expected concrete type.
    /// It is useful for internal operations where the concrete type is required.
    /// </para>
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// IDockable dockable = GetDockable();
    /// Dockable concreteDockable = dockable.AsDockable();
    /// ]]></code>
    /// </para>
    /// </remarks>
    internal static Dockable AsDockable(this IDockable dockable)
        => dockable as Dockable ?? throw new ArgumentException(
            $"Expecting an object that I created, i.e. `{typeof(Dockable)}`, but got an object of type `{dockable.GetType()}`",
            nameof(dockable));

    /// <summary>
    /// Casts an <see cref="IDock"/> to its concrete <see cref="Dock"/> implementation.
    /// </summary>
    /// <param name="dock">The <see cref="IDock"/> to cast.</param>
    /// <returns>The concrete <see cref="Dock"/> implementation.</returns>
    /// <exception cref="ArgumentException">
    /// Thrown when the <paramref name="dock"/> is not of type <see cref="Dock"/>.
    /// </exception>
    /// <remarks>
    /// <para>
    /// This method ensures that the provided <see cref="IDock"/> is of the expected concrete type.
    /// It is useful for internal operations where the concrete type is required.
    /// </para>
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// IDock dock = GetDock();
    /// Dock concreteDock = dock.AsDock();
    /// ]]></code>
    /// </para>
    /// </remarks>
    internal static Dock AsDock(this IDock dock)
        => dock as Dock ?? throw new ArgumentException(
            $"Expecting an object that I created, i.e. `{typeof(Dock)}`, but got an object of type `{dock.GetType()}`",
            nameof(dock));

    /// <summary>
    /// Casts an <see cref="IDockGroup"/> to its concrete <see cref="LayoutDockGroup"/> implementation.
    /// </summary>
    /// <param name="group">The <see cref="IDockGroup"/> to cast.</param>
    /// <returns>The concrete <see cref="LayoutDockGroup"/> implementation.</returns>
    /// <exception cref="ArgumentException">
    /// Thrown when the <paramref name="group"/> is not of type <see cref="LayoutDockGroup"/>.
    /// </exception>
    /// <remarks>
    /// <para>
    /// This method ensures that the provided <see cref="IDockGroup"/> is of the expected concrete type.
    /// It is useful for internal operations where the concrete type is required.
    /// </para>
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// IDockGroup group = GetDockGroup();
    /// LayoutDockGroup concreteGroup = group.AsDockGroup();
    /// ]]></code>
    /// </para>
    /// </remarks>
    internal static LayoutDockGroup AsDockGroup(this IDockGroup group)
        => group as LayoutDockGroup ?? throw new ArgumentException(
            $"Expecting an object that I created, i.e. `{typeof(LayoutDockGroup)}`, but got an object of type `{group.GetType()}`",
            nameof(group));
}
