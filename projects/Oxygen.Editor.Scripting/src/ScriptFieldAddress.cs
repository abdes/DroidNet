// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;

namespace Oxygen.Editor.Scripting;

/// <summary>
/// The address of a script slot, or of one of its parameter fields, on a node: <c>Scripting/{slot}</c> or
/// <c>Scripting/{slot}/{parameter}</c>. A problem that carries it leads to the field in the inspector.
/// </summary>
/// <param name="Slot">The slot identity.</param>
/// <param name="Parameter">The parameter name; <see langword="null"/> for the slot itself.</param>
public readonly record struct ScriptFieldAddress(Guid Slot, string? Parameter = null)
{
    /// <summary>The prefix of every script field address.</summary>
    public const string Prefix = "Scripting/";

    /// <summary>Parses an address.</summary>
    /// <param name="text">The text, such as a cook diagnostic's property path.</param>
    /// <param name="address">The address, when the text is one.</param>
    /// <returns>Whether the text addresses a script slot or field.</returns>
    public static bool TryParse(string? text, out ScriptFieldAddress address)
    {
        address = default;
        if (text?.StartsWith(Prefix, StringComparison.Ordinal) != true)
        {
            return false;
        }

        var parts = text[Prefix.Length..].Split('/', 2);
        if (!Guid.TryParse(parts[0], CultureInfo.InvariantCulture, out var slot))
        {
            return false;
        }

        address = new ScriptFieldAddress(slot, parts.Length > 1 ? parts[1] : null);
        return true;
    }

    /// <summary>Formats the address.</summary>
    /// <returns><c>Scripting/{slot}</c> or <c>Scripting/{slot}/{parameter}</c>.</returns>
    public override string ToString()
        => Prefix + this.Slot.ToString("D") + (this.Parameter is null ? string.Empty : "/" + this.Parameter);
}
