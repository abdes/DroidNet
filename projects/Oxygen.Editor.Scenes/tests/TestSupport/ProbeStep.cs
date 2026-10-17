// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>A step of <see cref="ProbeEdit"/>.</summary>
public enum ProbeStep
{
    /// <summary>The frame validated the probe.</summary>
    Validate,

    /// <summary>The frame applied the probe.</summary>
    Apply,
}
