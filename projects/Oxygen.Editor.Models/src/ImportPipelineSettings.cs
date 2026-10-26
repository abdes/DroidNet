// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Models;

/// <summary>One ImportTool pipeline's execution settings; an unset value takes the importer's default.</summary>
/// <param name="Workers">The pipeline's worker count, or <see langword="null"/> for the default.</param>
/// <param name="QueueCapacity">The capacity of its work queues, or <see langword="null"/> for the default.</param>
public sealed record ImportPipelineSettings(int? Workers, int? QueueCapacity)
{
    /// <summary>Gets a value indicating whether the pipeline sets neither value.</summary>
    public bool IsEmpty => this.Workers is null && this.QueueCapacity is null;
}
