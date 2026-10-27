// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Native classification of an indexed resource binding.</summary>
public enum CookedResourceBindingState
{
    /// <summary>The binding resolves to its resource.</summary>
    Resource,

    /// <summary>The binding could not be resolved.</summary>
    Error,

    /// <summary>The binding uses a fallback resource.</summary>
    Fallback,

    /// <summary>A texture another container holds, named by key.</summary>
    External,
}
