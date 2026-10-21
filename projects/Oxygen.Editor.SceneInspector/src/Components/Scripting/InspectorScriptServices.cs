// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scripting;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.SceneInspector.Components.Scripting;

/// <summary>The services the scripting inspector uses when the host provides them.</summary>
/// <param name="Scripts">The scripts, for script slot parameters and problems.</param>
/// <param name="CodeEditor">The code editor, for editing a slot's script.</param>
/// <param name="Creation">What creates a script for a slot, with New Script.</param>
/// <param name="Problems">What shows a script's problem in the Problems panel.</param>
internal sealed record InspectorScriptServices(IScripts? Scripts, ICodeEditorService? CodeEditor, IScriptCreation? Creation = null, IProblemReveal? Problems = null);
