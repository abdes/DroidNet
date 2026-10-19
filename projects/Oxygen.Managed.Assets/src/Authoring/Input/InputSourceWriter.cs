// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Nodes;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Authoring.Input;

/// <summary>Changes the engine's input source, <c>*.input.json</c>, keeping everything it does not change.</summary>
public static class InputSourceWriter
{
    /// <summary>
    /// Renames an action or a mapping context. A renamed action is renamed wherever the source names it by its name;
    /// references by virtual path, here and in other files, follow the product's new identity through relocation.
    /// </summary>
    /// <param name="jsonUtf8">The source bytes.</param>
    /// <param name="extension">The product's cooked extension: <see cref="AssetFileTypes.InputAction"/> or
    /// <see cref="AssetFileTypes.InputMappingContext"/>.</param>
    /// <param name="name">The product's name.</param>
    /// <param name="newName">The new name.</param>
    /// <returns>The changed source.</returns>
    /// <exception cref="AssetFormatException">
    /// The source cannot be read, does not declare the product (<see cref="InputSourceCodes.ProductMissing"/>), already
    /// declares the new name, also in another case (<see cref="InputSourceCodes.ProductNameTaken"/>), or the new name
    /// breaks the segment rule (the rule's <c>vpath.*</c> code).
    /// </exception>
    public static JsonObject RenameProduct(ReadOnlySpan<byte> jsonUtf8, string extension, string name, string newName)
    {
        ArgumentException.ThrowIfNullOrEmpty(name);
        ArgumentException.ThrowIfNullOrEmpty(newName);
        var isAction = extension switch
        {
            AssetFileTypes.InputAction => true,
            AssetFileTypes.InputMappingContext => false,
            _ => throw new ArgumentException("Only actions and mapping contexts are named inside an input source.", nameof(extension)),
        };

        var source = InputSourceReader.Read(jsonUtf8);
        var declared = isAction ? source.Actions.Select(static action => action.Name) : source.Contexts.Select(static context => context.Name);
        if (!declared.Contains(name, StringComparer.Ordinal))
        {
            throw DescriptorFormat.Problem(InputSourceCodes.ProductMissing, $"The input source declares no {Kind(isAction)} '{name}'.");
        }

        if (VirtualPath.ValidateSegment(newName + extension, VirtualPathRuleSet.Authored) is { } invalid)
        {
            throw DescriptorFormat.Problem(invalid.ToCode(), invalid.ToMessage());
        }

        if (declared.Any(other => !string.Equals(other, name, StringComparison.Ordinal) && VirtualPathComparer.CaseFolded.Equals(other, newName)))
        {
            throw DescriptorFormat.Problem(InputSourceCodes.ProductNameTaken, $"The input source already declares a {Kind(isAction)} named '{newName}'.");
        }

        var document = JsonNode.Parse(DescriptorFormat.WithoutByteOrderMark(jsonUtf8), documentOptions: new JsonDocumentOptions
        {
            AllowTrailingCommas = true,
            CommentHandling = JsonCommentHandling.Skip,
        })!.AsObject();
        foreach (var declaration in Items(document, isAction ? "actions" : "contexts").Where(item => Is(item["name"], name)))
        {
            declaration["name"] = newName;
        }

        if (isAction)
        {
            foreach (var reference in LocalActionReferences(document).Where(reference => Is(reference.Parent[reference.Property], name)))
            {
                reference.Parent[reference.Property] = newName;
            }
        }

        return document;
    }

    private static string Kind(bool isAction) => isAction ? "action" : "mapping context";

    private static bool Is(JsonNode? node, string name)
        => node is JsonValue value && value.TryGetValue<string>(out var text) && string.Equals(text, name, StringComparison.Ordinal);

    // The same fields the reader collects as action references.
    private static IEnumerable<(JsonObject Parent, string Property)> LocalActionReferences(JsonObject document)
    {
        foreach (var mapping in Items(document, "contexts").SelectMany(static context => Items(context, "mappings")))
        {
            yield return (mapping, "action");
            foreach (var trigger in Items(mapping, "triggers"))
            {
                yield return (trigger, "chord_action");
                foreach (var step in Items(trigger, "combo_actions").Concat(Items(trigger, "aux")))
                {
                    yield return (step, "action");
                }
            }
        }
    }

    private static IEnumerable<JsonObject> Items(JsonObject parent, string property)
        => parent[property] is JsonArray items ? items.OfType<JsonObject>() : [];
}
