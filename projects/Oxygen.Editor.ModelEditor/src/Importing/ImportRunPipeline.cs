// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ModelEditor.Importing;

/// <summary>One row of the Import run's Pipelines grid: a pipeline and its two values.</summary>
/// <param name="Name">The pipeline's manifest name, such as <c>mesh_build</c>.</param>
/// <param name="Label">The row header, such as Mesh build.</param>
/// <param name="Workers">The pipeline's workers.</param>
/// <param name="Queue">The capacity of its work queues.</param>
public sealed record ImportRunPipeline(string Name, string Label, RunValue Workers, RunValue Queue);
