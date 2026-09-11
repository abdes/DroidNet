// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>Identifies a material gesture within one open document instance.</summary>
/// <param name="DocumentId">The open document identity.</param>
/// <param name="SessionId">The gesture identity.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct MaterialEditSession(Guid DocumentId, Guid SessionId);
