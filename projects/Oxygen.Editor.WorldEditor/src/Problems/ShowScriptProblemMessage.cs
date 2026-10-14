// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentBrowser.Scripts;

namespace Oxygen.Editor.World.Problems;

/// <summary>Brings the Problems panel forward with one problem selected.</summary>
/// <param name="Problem">The problem to select.</param>
public sealed record ShowScriptProblemMessage(ScriptProblem Problem);
