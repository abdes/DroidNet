// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// Directional light option for scene sun binding.
/// </summary>
/// <param name="NodeId">The scene node id, or <see langword="null"/> for no sun binding.</param>
/// <param name="DisplayName">The display name shown in the environment inspector.</param>
/// <param name="IsEnabled">Whether the light is available for this atmosphere role.</param>
public sealed record SunLightOption(Guid? NodeId, string DisplayName, bool IsEnabled = true);
