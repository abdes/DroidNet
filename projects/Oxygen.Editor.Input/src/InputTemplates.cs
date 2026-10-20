// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Input;

/// <summary>The text of a new input source.</summary>
public static class InputTemplates
{
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
