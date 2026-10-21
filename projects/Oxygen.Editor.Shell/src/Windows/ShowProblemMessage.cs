// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Diagnostics;

namespace Oxygen.Editor.Shell.Windows;

/// <summary>Asks the window's Problems panel to select a problem and come forward.</summary>
/// <param name="Problem">The problem to select.</param>
public sealed record ShowProblemMessage(AssetProblem Problem);
