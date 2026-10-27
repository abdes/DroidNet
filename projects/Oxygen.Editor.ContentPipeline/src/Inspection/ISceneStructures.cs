// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>
/// What the instances of a project session's scenes contain (scene instances 8.2): the last structure read of each
/// source, which the source's nodes and those of the scenes it instances make up.
/// </summary>
/// <remarks>
/// The engine that shows the scene reads the structures; the edit checks, the lock rule and the views look them up
/// synchronously, from any thread. A structure is kept until a newer read of its source replaces it.
/// </remarks>
public interface ISceneStructures
{
    /// <summary>Occurs, on the thread that read them, after structures were read or read again.</summary>
    public event EventHandler? Changed;

    /// <summary>Gets the structure last read of a source.</summary>
    /// <param name="project">The originating project.</param>
    /// <param name="source">A model's source file or an authored scene.</param>
    /// <returns>
    /// The structure, or null when none was read: the project shows no scene that instances the source, or the source
    /// is not instanceable or not in the mounted content yet.
    /// </returns>
    public SceneStructure? Find(ProjectContext project, AssetPath source);
}
