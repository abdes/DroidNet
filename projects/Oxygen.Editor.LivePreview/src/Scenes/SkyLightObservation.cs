// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>What the engine reports about the sky light of the scene it shows.</summary>
/// <param name="Observed">Whether the engine reported the sky light at all.</param>
/// <param name="Enabled">Whether the sky light is enabled.</param>
/// <param name="UnavailableReason">Why the sky light casts no light, if it casts none.</param>
/// <param name="EmptyCapture">Whether the sky capture has nothing to capture.</param>
public sealed record SkyLightObservation(bool Observed, bool Enabled, SkyLightUnavailableReason UnavailableReason, bool EmptyCapture);
