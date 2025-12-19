// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// Defines the hard limits enforced by the engine when allocating Composition surfaces.
/// </summary>
public static class EngineConstants
{
    /// <summary>The minimum allowed logging verbosity for the native engine.</summary>
    public const int MinLoggingVerbosity = -9; // loguru's minimum verbosity

    /// <summary>The maximum allowed logging verbosity for the native engine.</summary>
    public const int MaxLoggingVerbosity = 9; // loguru's maximum verbosity

    /// <summary>The maximum number of simultaneously active surfaces supported by the native engine.</summary>
    public const int MaxTotalSurfaces = 8;

    /// <summary>The maximum number of composition surfaces a single document may keep alive.</summary>
    public const int MaxSurfacesPerDocument = 4;
}
