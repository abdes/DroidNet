// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Managed renderer implementation options, converted at the native session boundary.</summary>
public enum RendererImplementation
{
    /// <summary>Legacy.</summary>
    Legacy = 0,

    /// <summary>Vortex.</summary>
    Vortex = 1,
}
