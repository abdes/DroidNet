// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>Why the last Play failed.</summary>
/// <param name="Code">The diagnostic code of the published operation result.</param>
/// <param name="Summary">A few words for the status, for example "Cook failed".</param>
/// <param name="Message">What happened and what to do.</param>
/// <param name="Stage">The step that failed.</param>
/// <param name="ErrorCount">How many errors the failed step reported; 0 when it does not count them.</param>
public sealed record PlayFailure(string Code, string Summary, string Message, PlayStage Stage, int ErrorCount = 0);
