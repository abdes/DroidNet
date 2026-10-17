// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.LivePreview.Session;

/// <summary>Why the preview of the open project is unavailable.</summary>
/// <param name="Code">The diagnostic code of the published operation result.</param>
/// <param name="Title">The failed capability.</param>
/// <param name="Message">The failure summary shown beside the recovery actions.</param>
public sealed record PreviewFailure(string Code, string Title, string Message);
