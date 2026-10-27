// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>A descriptor-local native resource binding, not a cooked asset dependency.</summary>
/// <param name="Kind">The stable native resource kind.</param>
/// <param name="Index">The descriptor-local binding index.</param>
/// <param name="State">The native fallback/error classification when applicable.</param>
public sealed record CookedResourceBinding(CookedResourceKind Kind, uint Index, CookedResourceBindingState State);
