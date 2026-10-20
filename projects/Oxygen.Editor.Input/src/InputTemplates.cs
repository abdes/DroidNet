// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;

namespace Oxygen.Editor.Input;

/// <summary>The text of a new input source, and the template New Input File creates it from.</summary>
public static class InputTemplates
{
    /// <summary>Gets the template of New Input File: an input source in <c>Input</c>, its mapping context named after it.</summary>
    public static AuthoredAssetTemplate Asset { get; } = new()
    {
        Folder = "Input",
        DefaultName = "NewInput",
        Suffixes = [AssetFileTypes.InputFile],
        Content = New,
    };

    /// <summary>Creates the text of a new input source: one action and one mapping context named after the file.</summary>
    /// <param name="contextName">The mapping context name, usually the file name.</param>
    /// <returns>The input source text, valid against <c>oxygen.input.schema.json</c>.</returns>
    public static string New(string contextName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(contextName);
        return $$"""
            {
              "actions": [
                {
                  "name": "Jump",
                  "type": "bool"
                }
              ],
              "contexts": [
                {
                  "name": "{{contextName}}",
                  "mappings": [
                    {
                      "action": "Jump",
                      "slot": "Space",
                      "trigger": "pressed"
                    }
                  ]
                }
              ]
            }

            """;
    }
}
