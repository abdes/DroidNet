// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>A complete line from an owned native worker's output.</summary>
/// <param name="Text">The line without its line terminator.</param>
/// <param name="IsStandardError">Whether it came from stderr; this is not a diagnostic severity.</param>
public sealed record ContentPipelineProcessOutput(string Text, bool IsStandardError);
