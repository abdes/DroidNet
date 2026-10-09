// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>One rewritten authored file.</summary>
/// <param name="OriginalPath">The file's location before the moves.</param>
/// <param name="FinalPath">The file's location after the moves.</param>
/// <param name="Before">The baseline version the rewrite was prepared from.</param>
/// <param name="BeforeContent">The baseline bytes, restored on rollback.</param>
/// <param name="AfterContent">The rewritten bytes.</param>
internal sealed record RelocationEdit(string OriginalPath, string FinalPath, FileVersion Before, byte[] BeforeContent, byte[] AfterContent);
