// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Threading;
using System.Threading.Tasks;
using Oxygen.Editor.Schemas;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Schema-driven property edit entry point for material documents.
/// </summary>
/// <remarks>
/// <para>
/// This is the property pipeline's plug-in for the material editor.
/// Callers obtain typed identities from
/// <see cref="MaterialDescriptors.Catalog"/> and build a
/// <see cref="PropertyEdit"/> map; the service applies it to the
/// document and validates the resulting JSON against the merged engine
/// + overlay schema before marking the document dirty.
/// </para>
/// </remarks>
public interface IMaterialPropertyEditService
{
    /// <summary>
    /// Applies a schema-driven property edit to a material document.
    /// </summary>
    /// <param name="documentId">The material document id.</param>
    /// <param name="edit">The property edit map.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The edit result.</returns>
    Task<MaterialEditResult> EditPropertiesAsync(
        Guid documentId,
        PropertyEdit edit,
        CancellationToken cancellationToken = default);
}
