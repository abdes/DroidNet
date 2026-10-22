// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;
using Microsoft.UI;

namespace DroidNet.Aura.Documents;

/// <summary>A document a tab drag is moving, the window it came from, and the source of that window's tabs.</summary>
/// <param name="Document">The document.</param>
/// <param name="Source">The window whose tabs detached it.</param>
/// <param name="Tabs">The tab source that detached it, the only one that can attach it again.</param>
internal sealed record DocumentTransfer(IDocumentMetadata Document, WindowId Source, IDocumentTabSource Tabs);
