// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Config;

namespace Oxygen.Editor.Core.Services;

/// <summary>
/// Provides methods to find paths for Oxygen projects.
/// </summary>
public interface IOxygenPathFinder : IPathFinder
{
    /// <summary>
    /// Gets the path to the personal projects.
    /// </summary>
    public string PersonalProjects { get; }

    /// <summary>
    /// Gets the path to the local projects.
    /// </summary>
    public string LocalProjects { get; }

    /// <summary>
    /// Gets the path to the database file used to store the application runtime state.
    /// </summary>
    public string StateDatabasePath { get; }
}
