// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Testing;

/// <summary>A document of one authored format with its identity; formats start from a minimal document their schema accepts.</summary>
/// <typeparam name="TSelf">The format's document type.</typeparam>
public abstract class AuthoredDocument<TSelf>
    where TSelf : AuthoredDocument<TSelf>
{
    private static readonly JsonSerializerOptions WriteOptions = new() { WriteIndented = true };

    /// <summary>Initializes a new instance of the <see cref="AuthoredDocument{TSelf}"/> class.</summary>
    /// <param name="identity">The document's identity.</param>
    /// <param name="json">The format's minimal document.</param>
    protected AuthoredDocument(AssetPath identity, JsonObject json)
    {
        this.Identity = identity ?? throw new ArgumentNullException(nameof(identity));
        this.Json = json ?? throw new ArgumentNullException(nameof(json));
    }

    /// <summary>Gets the document's identity.</summary>
    public AssetPath Identity { get; }

    /// <summary>Gets the document's JSON.</summary>
    public JsonObject Json { get; }

    /// <summary>Gets the document's text.</summary>
    public string Text => this.Json.ToJsonString(WriteOptions);

    /// <summary>Gets the document's UTF-8 bytes.</summary>
    public byte[] Bytes => Encoding.UTF8.GetBytes(this.Text);

    /// <summary>Gets the text this document writes to name <paramref name="target"/>.</summary>
    /// <param name="target">The identity the reference names.</param>
    /// <returns>The reference text.</returns>
    public string Spell(string target) => AuthoredFiles.SpellReference(this.Identity, AssetPath.Parse(target));

    /// <summary>Sets the field at a JSON pointer, as written, creating the objects above it.</summary>
    /// <param name="pointer">A JSON pointer such as <c>/textures/base_color/uv_set</c>.</param>
    /// <param name="value">The value, written as it is.</param>
    /// <returns>This document.</returns>
    public TSelf Set(string pointer, JsonNode? value)
    {
        var (parent, key) = this.Locate(pointer);
        if (parent is JsonArray array)
        {
            array[int.Parse(key, CultureInfo.InvariantCulture)] = value;
        }
        else
        {
            parent.AsObject()[key] = value;
        }

        return (TSelf)this;
    }

    /// <summary>Removes the field or array element at a JSON pointer.</summary>
    /// <param name="pointer">A JSON pointer such as <c>/textures/normal</c>.</param>
    /// <returns>This document.</returns>
    public TSelf Remove(string pointer)
    {
        var (parent, key) = this.Locate(pointer);
        if (parent is JsonArray array)
        {
            array.RemoveAt(int.Parse(key, CultureInfo.InvariantCulture));
        }
        else
        {
            _ = parent.AsObject().Remove(key);
        }

        return (TSelf)this;
    }

    /// <summary>Sets the reference field at a JSON pointer to name <paramref name="target"/>.</summary>
    /// <param name="pointer">The reference field's JSON pointer.</param>
    /// <param name="target">The identity the reference names.</param>
    /// <returns>This document.</returns>
    public TSelf Reference(string pointer, string target) => this.Set(pointer, this.Spell(target));

    /// <summary>Appends a value to the array at a JSON pointer, creating the array.</summary>
    /// <param name="pointer">The array's JSON pointer.</param>
    /// <param name="value">The value, written as it is.</param>
    /// <returns>This document.</returns>
    public TSelf Append(string pointer, JsonNode? value)
    {
        var (parent, key) = this.Locate(pointer);
        var container = parent is JsonArray parentArray
            ? parentArray[int.Parse(key, CultureInfo.InvariantCulture)]
            : parent.AsObject()[key];
        if (container is null)
        {
            container = new JsonArray();
            _ = this.Set(pointer, container);
        }

        container.AsArray().Add(value);
        return (TSelf)this;
    }

    /// <summary>Gets the stem of the document's identity (<c>Wall</c> for <c>/Content/Materials/Wall.omat</c>).</summary>
    /// <param name="identity">The identity.</param>
    /// <returns>The leaf without its extension.</returns>
    protected static string StemOf(AssetPath identity)
    {
        ArgumentNullException.ThrowIfNull(identity);
        return Path.GetFileNameWithoutExtension(identity.Name);
    }

    private (JsonNode Parent, string Key) Locate(string pointer)
    {
        ArgumentException.ThrowIfNullOrEmpty(pointer);
        if (pointer[0] != '/')
        {
            throw new ArgumentException($"'{pointer}' is not a JSON pointer.", nameof(pointer));
        }

        var tokens = pointer[1..].Split('/').Select(static t => t.Replace("~1", "/", StringComparison.Ordinal).Replace("~0", "~", StringComparison.Ordinal)).ToArray();
        JsonNode node = this.Json;
        foreach (var token in tokens[..^1])
        {
            if (node is JsonArray array)
            {
                node = array[int.Parse(token, CultureInfo.InvariantCulture)]
                    ?? throw new ArgumentException($"'{pointer}' passes through a null element.", nameof(pointer));
                continue;
            }

            var current = node.AsObject();
            if (current[token] is not { } next)
            {
                next = new JsonObject();
                current[token] = next;
            }

            node = next;
        }

        return (node, tokens[^1]);
    }
}
