// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.Projects;

/// <summary>Owns staged project metadata without changing the active project lifetime.</summary>
public sealed class ProjectLoadSnapshot
{
    /// <summary>Initializes a new instance of the <see cref="ProjectLoadSnapshot"/> class.Initializes an owned project staging lifetime.</summary>
    /// <param name="owner">The staging service.</param>
    /// <param name="project">The staged project metadata.</param>
    /// <param name="previousProject">The accepted project this staging lifetime would replace.</param>
    internal ProjectLoadSnapshot(ProjectManagerService owner, IProject project, IProject? previousProject)
    {
        this.Owner = owner;
        this.Project = project;
        this.PreviousProject = previousProject;
    }

    /// <summary>Gets the staged project, containing scene metadata rather than loaded editing graphs.</summary>
    public IProject Project { get; }

    /// <summary>Gets the service authorized to activate this project.</summary>
    internal ProjectManagerService Owner { get; }

    /// <summary>Gets the accepted project observed when staging began.</summary>
    internal IProject? PreviousProject { get; }

    /// <summary>Gets or sets a value indicating whether this one-shot staging lifetime was accepted.</summary>
    internal bool Accepted { get; set; }
}
