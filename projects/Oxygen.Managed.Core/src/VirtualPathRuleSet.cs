// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core;

/// <summary>Which rules a virtual path is checked against.</summary>
public enum VirtualPathRuleSet
{
    /// <summary>A path the engine writes or resolves, such as a reference to an asset.</summary>
    Cooked,

    /// <summary>A path being authored; adds the derived content rule.</summary>
    Authored,
}
