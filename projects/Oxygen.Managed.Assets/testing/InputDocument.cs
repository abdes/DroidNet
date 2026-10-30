// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Testing;

/// <summary>An input source (<c>*.input.json</c>).</summary>
public sealed class InputDocument : AuthoredDocument<InputDocument>
{
    /// <summary>Initializes a new instance of the <see cref="InputDocument"/> class.</summary>
    /// <param name="identity">The source's identity.</param>
    public InputDocument(AssetPath identity)
        : base(identity, new JsonObject { ["actions"] = new JsonArray(), ["contexts"] = new JsonArray() })
    {
    }

    /// <summary>Declares an action.</summary>
    /// <param name="name">The action's name.</param>
    /// <param name="type">The action's type: <c>bool</c>, <c>axis1d</c> or <c>axis2d</c>.</param>
    /// <returns>This document.</returns>
    public InputDocument Action(string name, string type)
        => this.Append("/actions", new JsonObject { ["name"] = name, ["type"] = type });

    /// <summary>Declares a mapping context with no mappings.</summary>
    /// <param name="name">The context's name.</param>
    /// <param name="priority">The context's priority.</param>
    /// <returns>This document.</returns>
    public InputDocument Context(string name, int priority = 0)
        => this.Append("/contexts", new JsonObject { ["name"] = name, ["priority"] = priority, ["mappings"] = new JsonArray() });

    /// <summary>Gets the text a mapping of this source writes to use the action <paramref name="target"/> of another source.</summary>
    /// <param name="target">The action's identity.</param>
    /// <returns>The action reference text.</returns>
    public string ActionReference(string target) => this.Spell(target);
}
