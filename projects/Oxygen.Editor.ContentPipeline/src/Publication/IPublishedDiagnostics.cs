// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>
/// Reads the diagnostics kept with a source's published product: the warnings its last cook or import reported about
/// it and its outputs, which still apply while the product is reused.
/// </summary>
/// <remarks>
/// They are kept in the project's selected publication, so they read the same in a later session as right after the
/// import, such as a model's missing texture or dropped animation.
/// </remarks>
public interface IPublishedDiagnostics
{
    /// <summary>Gets the diagnostics kept with a source's product in the project's selected publication.</summary>
    /// <param name="project">The project whose publication is read.</param>
    /// <param name="source">The source, such as a model's source file.</param>
    /// <param name="cancellationToken">Cancels the read.</param>
    /// <returns>The diagnostics; empty when the source has no published product or none were kept.</returns>
    public Task<ImmutableArray<DiagnosticRecord>> ReadDiagnosticsAsync(ProjectContext project, AssetPath source, CancellationToken cancellationToken = default);
}
