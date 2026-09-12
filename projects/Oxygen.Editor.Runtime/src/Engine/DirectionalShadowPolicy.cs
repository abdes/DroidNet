// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Managed directional shadow policy options, converted at the native session boundary.</summary>
public enum DirectionalShadowPolicy
{
    /// <summary>Conventional Only.</summary>
    ConventionalOnly = 0,

    /// <summary>Virtual Shadow Map.</summary>
    VirtualShadowMap = 1,
}
