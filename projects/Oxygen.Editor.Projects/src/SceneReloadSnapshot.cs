// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Projects;

/// <summary>An owned disk read awaiting the document owner's revision and lifetime checks.</summary>
public sealed class SceneReloadSnapshot
{
    /// <summary>Initializes a new instance of the <see cref="SceneReloadSnapshot"/> class.</summary>
    /// <param name="owner">The service that read the source.</param>
    /// <param name="original">The model whose source was read.</param>
    /// <param name="replacement">The independently loaded model.</param>
    /// <param name="sourcePath">The storage destination.</param>
    /// <param name="version">The exact version read from disk.</param>
    internal SceneReloadSnapshot(ProjectManagerService owner, Scene original, Scene replacement, string sourcePath, FileVersion version)
    {
        this.Owner = owner;
        this.Original = original;
        this.Scene = replacement;
        this.SourcePath = sourcePath;
        this.Version = version;
    }

    /// <summary>Gets the replacement authoring model, which is not yet installed in the project.</summary>
    public Scene Scene { get; }

    /// <summary>Gets the service authorized to adopt this read's baseline.</summary>
    internal ProjectManagerService Owner { get; }

    /// <summary>Gets the original model identity, which must still belong to the project.</summary>
    internal Scene Original { get; }

    /// <summary>Gets the source destination whose baseline will be adopted.</summary>
    internal string SourcePath { get; }

    /// <summary>Gets the exact disk version accompanying the replacement.</summary>
    internal FileVersion Version { get; }
}
