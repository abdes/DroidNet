// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Identifies a live viewport without exposing an interop view handle.</summary>
/// <param name="RunId">The owning runtime run.</param>
/// <param name="DocumentId">The owning document.</param>
/// <param name="ViewportId">The owning viewport.</param>
/// <param name="Generation">The view creation identity, invalidated before destruction.</param>
/// <param name="ViewId">The process-local view identifier; never persist it.</param>
[StructLayout(LayoutKind.Auto)]
public readonly record struct RuntimeViewTarget(Guid RunId, Guid DocumentId, Guid ViewportId, Guid Generation, ulong ViewId);
