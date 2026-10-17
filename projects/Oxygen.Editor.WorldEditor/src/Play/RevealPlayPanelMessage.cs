// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Play;

/// <summary>Asks the workspace to bring forward the panel that explains a Play state.</summary>
/// <param name="Panel">The panel.</param>
internal sealed record RevealPlayPanelMessage(PlayPanel Panel);
