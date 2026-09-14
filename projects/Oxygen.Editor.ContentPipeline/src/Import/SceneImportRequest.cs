// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>The reviewed source and destination for one explicit model import.</summary>
/// <param name="Project">The project whose destination the user reviewed.</param>
/// <param name="SourcePath">The selected primary source file.</param>
/// <param name="Name">The source bundle and output folder name.</param>
/// <param name="DestinationFolder">The authoring folder containing this model's output folder.</param>
public sealed record SceneImportRequest(ProjectContext Project, string SourcePath, string Name, Uri DestinationFolder)
{
    /// <summary>Gets source facts retained by an earlier attempt in this editor session.</summary>
    internal RetainedImportSource? RetainedSource { get; init; }
}
