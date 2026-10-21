// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Views;

/// <summary>Which scene camera a viewport looks through.</summary>
/// <param name="ViewedCameraId">The camera the viewport looks through, or <see langword="null"/> for the editor's camera.</param>
/// <param name="IsPiloting">Whether the viewport's navigation moves that camera.</param>
public sealed record SceneCameraViewState(NodeId? ViewedCameraId, bool IsPiloting);
