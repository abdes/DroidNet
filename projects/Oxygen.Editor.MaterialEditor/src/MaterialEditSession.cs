// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using DroidNet.Documents;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
///     Names an edit gesture on a material document, such as dragging a slider: its previews and its end.
/// </summary>
/// <param name="DocumentId">The material document the gesture edits.</param>
/// <param name="SessionId">The gesture's own id.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct MaterialEditSession(DocumentId DocumentId, Guid SessionId);
