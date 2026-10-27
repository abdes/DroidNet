// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>A producer-owned source transition committed with cooked output.</summary>
/// <param name="RelativePath">The source file's path relative to the project.</param>
/// <param name="BeforeHash">The file's hash before the cook rewrote it.</param>
/// <param name="AfterHash">The file's hash after the cook rewrote it.</param>
internal sealed record CookPublicationSourceTransition(string RelativePath, string BeforeHash, string AfterHash);
