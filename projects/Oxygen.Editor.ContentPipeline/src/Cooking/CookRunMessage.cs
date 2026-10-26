// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>A message belonging exclusively to one cook's session transcript.</summary>
/// <param name="Sequence">The ordered position within this run.</param>
/// <param name="Timestamp">The time the message was received.</param>
/// <param name="Severity">The message's severity.</param>
/// <param name="Text">The complete readable or native message text.</param>
/// <param name="AssetUri">The affected asset, when known.</param>
/// <param name="Detail">Technical text for the message's details, such as where in its source a problem is; null when there is none.</param>
public sealed record CookRunMessage(long Sequence, DateTimeOffset Timestamp, DiagnosticSeverity Severity, string Text, AssetPath? AssetUri = null, string? Detail = null);
