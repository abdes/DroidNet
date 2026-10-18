// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// The component class a runtime property targets, at the managed runtime boundary.
/// </summary>
/// <remarks>
/// The names and values are those of the native wire vocabulary, <c>Oxygen.Interop.EngineComponentId</c>. Runtime owns
/// this copy so its public commands load without Interop; the native transport converts between the two, and a Runtime
/// test keeps them identical.
/// </remarks>
public enum RuntimeComponentId : ushort
{
    /// <summary>Sentinel; not a valid wire id.</summary>
    Invalid = 0,

    /// <summary>The node's transform.</summary>
    Transform = 1,

    /// <summary>A perspective camera.</summary>
    PerspectiveCamera = 2,

    /// <summary>A directional light.</summary>
    DirectionalLight = 3,

    /// <summary>An orthographic camera.</summary>
    OrthographicCamera = 4,

    /// <summary>A point light.</summary>
    PointLight = 5,

    /// <summary>A spot light.</summary>
    SpotLight = 6,

    /// <summary>Authored node rendering flags, applied as local scene-node values.</summary>
    Node = 7,

    /// <summary>A local fog volume.</summary>
    LocalFogVolume = 8,
}
