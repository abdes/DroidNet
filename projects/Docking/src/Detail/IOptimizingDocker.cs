// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking.Workspace;

namespace DroidNet.Docking.Detail;

/// <summary>
/// Defines methods for optimizing the docking tree structure within a workspace.
/// </summary>
/// <remarks>
/// Implementing this interface allows for the consolidation and optimization of dock groups to maintain an efficient and clean docking layout.
/// </remarks>
internal interface IOptimizingDocker
{
    /// <summary>
    /// Consolidates the docking tree starting from the specified dock group and moving upwards.
    /// </summary>
    /// <param name="startingGroup">The dock group from which to start the consolidation process.</param>
    /// <remarks>
    /// This method is used to optimize the docking tree by merging or removing unnecessary groups, ensuring a more efficient layout.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// IOptimizingDocker optimizer = ...;
    /// IDockGroup startingGroup = ...;
    /// optimizer.ConsolidateUp(startingGroup);
    /// ]]></code>
    /// </para>
    /// <para>
    /// <strong>Guidelines:</strong>
    /// <list type="bullet">
    /// <item>Ensure that the <paramref name="startingGroup"/> is part of the current docking tree.</item>
    /// <item>Handle any exceptions that might occur during the consolidation process to avoid runtime errors.</item>
    /// </list>
    /// </para>
    /// </remarks>
    void ConsolidateUp(IDockGroup startingGroup);
}
