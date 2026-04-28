// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Resolves material source asset URIs to project filesystem locations.
/// </summary>
public interface IMaterialSourcePathResolver
{
    /// <summary>
    /// Resolves a material source URI to its project location.
    /// </summary>
    /// <param name="materialUri">The material source asset URI.</param>
    /// <returns>The resolved source location.</returns>
    MaterialSourceLocation Resolve(Uri materialUri);
}
