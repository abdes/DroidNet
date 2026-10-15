// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Play;

/// <summary>Why the last Play failed.</summary>
/// <param name="Summary">A few words for the status, for example "Cook failed".</param>
/// <param name="Message">What happened and what to do.</param>
/// <param name="Panel">The panel with the details.</param>
/// <param name="ErrorCount">How many errors that panel lists for this failure; 0 when it does not count them.</param>
public sealed record PlayFailure(string Summary, string Message, PlayPanel Panel, int ErrorCount = 0);
