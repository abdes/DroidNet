// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;

namespace DroidNet.Docking.Workspace;

/// <summary>
/// Represents a group of docks within a docking workspace.
/// </summary>
/// <remarks>
/// The <see cref="IDockGroup"/> interface extends <see cref="ILayoutSegment"/> and provides a collection of docks that are managed as a group.
/// Implementing this interface allows for the organization and management of multiple docks within a single layout segment.
/// </remarks>
public interface IDockGroup : ILayoutSegment
{
    /// <summary>
    /// Gets a read-only collection of docks contained within the group.
    /// </summary>
    /// <value>
    /// A <see cref="ReadOnlyObservableCollection{T}"/> of <see cref="IDock"/> representing the docks in the group.
    /// </value>
    /// <remarks>
    /// This property provides access to the docks managed by the group. The collection is read-only to ensure that docks are only added or removed through the group's methods.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// IDockGroup dockGroup = ...;
    /// foreach (var dock in dockGroup.Docks)
    /// {
    ///     // Process each dock
    /// }
    /// ]]></code>
    /// </para>
    /// </remarks>
    public ReadOnlyObservableCollection<IDock> Docks { get; }
}
