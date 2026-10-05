// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Projects;

/// <summary>Owns validated scene data until the workspace approves retiring its previous editable graph.</summary>
public sealed class SceneLoadSnapshot
{
    /// <summary>Initializes owned, validated source data for a later workspace handoff.</summary>
    /// <param name="owner">The staging service.</param>
    /// <param name="original">The still-current scene metadata.</param>
    /// <param name="data">The validated data without an editable graph.</param>
    /// <param name="sourcePath">The saved source destination.</param>
    /// <param name="version">The exact source version read.</param>
    internal SceneLoadSnapshot(ProjectManagerService owner, Scene original, SceneData data, string sourcePath, FileVersion version)
    {
        this.Owner = owner;
        this.Original = original;
        this.Data = data;
        this.SourcePath = sourcePath;
        this.Version = version;
    }

    /// <summary>Gets the staged asset identity.</summary>
    public Guid SceneId => this.Data.Id;

    /// <summary>Gets the service authorized to adopt this source.</summary>
    internal ProjectManagerService Owner { get; }

    /// <summary>Gets the metadata identity that must still belong to the project.</summary>
    internal Scene Original { get; }

    /// <summary>Gets the private staging payload.</summary>
    internal SceneData Data { get; }

    /// <summary>Gets the saved source destination.</summary>
    internal string SourcePath { get; }

    /// <summary>Gets the source version to adopt only upon acceptance.</summary>
    internal FileVersion Version { get; }

    /// <summary>Gets or sets whether this one-shot staging payload was accepted.</summary>
    internal bool Accepted { get; set; }
}
