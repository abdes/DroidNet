// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using DroidNet.TimeMachine;

namespace Oxygen.Editor.Documents;

/// <summary>The identity and the history of the document a kind decodes content for.</summary>
/// <remarks>
/// The document service creates both before it decodes a file, and the document takes the same pair, so content that
/// records edits is complete when it is decoded. A reload decodes with the document's own pair. Content records into the
/// history it is given and never creates, finds or clears one.
/// </remarks>
/// <param name="Document">The identity of the document.</param>
/// <param name="History">The document's history.</param>
public sealed record DocumentDecodeContext(DocumentId Document, HistoryKeeper History);
