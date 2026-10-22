// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>Why an edit stopped, or what the user should know about an edit that applied.</summary>
/// <remarks>
/// The code is the contract: callers, remote clients and tests branch on it. The message is for people and may change.
/// </remarks>
/// <param name="IsError">Whether the edit stopped; otherwise it applied and this is a warning.</param>
/// <param name="Code">The rule that produced it, one of <see cref="SceneEditCodes"/>.</param>
/// <param name="Message">What happened, as people read it.</param>
/// <param name="Node">The node it is about, if any.</param>
/// <param name="Property">The property a value rule rejected, if any.</param>
public sealed record SceneDiagnostic(bool IsError, string Code, string Message, NodeId? Node = null, PropertyId? Property = null)
{
    /// <summary>Creates an error: the edit stopped and nothing changed.</summary>
    /// <param name="code">The rule that stopped the edit, one of <see cref="SceneEditCodes"/>.</param>
    /// <param name="message">What stopped the edit, as people read it.</param>
    /// <param name="node">The node it is about, if any.</param>
    /// <param name="property">The property a value rule rejected, if any.</param>
    /// <returns>The diagnostic.</returns>
    public static SceneDiagnostic Error(string code, string message, NodeId? node = null, PropertyId? property = null)
        => new(IsError: true, code, message, node, property);

    /// <summary>Creates a warning: the edit applied, such as with a reference to a deleted asset.</summary>
    /// <param name="code">The rule that produced the warning, one of <see cref="SceneEditCodes"/>.</param>
    /// <param name="message">What the user should know, as people read it.</param>
    /// <param name="node">The node it is about, if any.</param>
    /// <returns>The diagnostic.</returns>
    public static SceneDiagnostic Warning(string code, string message, NodeId? node = null) => new(IsError: false, code, message, node);
}
