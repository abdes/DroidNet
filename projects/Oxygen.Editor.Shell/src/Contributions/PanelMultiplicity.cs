// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Contributions;

/// <summary>How many instances of a panel a session has.</summary>
public enum PanelMultiplicity
{
    /// <summary>One instance, shown in at most one window at a time.</summary>
    Single,

    /// <summary>Any number of independent instances.</summary>
    Many,
}
