// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Stable wire identifier for the component class an editor property targets.
/// Mirrors <c>oxygen::interop::module::ComponentId</c>.
/// </summary>
/// <remarks>
/// <para>
/// This is the coarse dimension of the property-pipeline wire vocabulary.
/// The fine dimension is a component-local field id owned by the matching
/// native applier class.
/// </para>
/// <para>
/// Adding a new component class is one new entry here, one native applier,
/// and one registration call. Adding a new property to an existing component
/// touches neither this enum nor the transport.
/// </para>
/// </remarks>
public enum EngineComponentId
{
    /// <summary>Sentinel; not a valid wire id.</summary>
    Invalid = 0,

    /// <summary><c>oxygen::scene::TransformComponent</c>.</summary>
    Transform = 1,

    /// <summary><c>oxygen::scene::PerspectiveCamera</c>.</summary>
    PerspectiveCamera = 2,

    /// <summary><c>oxygen::scene::DirectionalLight</c>.</summary>
    DirectionalLight = 3,

    /// <summary><c>oxygen::scene::OrthographicCamera</c>.</summary>
    OrthographicCamera = 4,

    /// <summary><c>oxygen::scene::PointLight</c>.</summary>
    PointLight = 5,

    /// <summary><c>oxygen::scene::SpotLight</c>.</summary>
    SpotLight = 6,

    /// <summary>Authored node rendering flags, applied as local scene-node values.</summary>
    Node = 7,

    /// <summary><c>oxygen::scene::environment::LocalFogVolume</c>.</summary>
    LocalFogVolume = 8,
}
