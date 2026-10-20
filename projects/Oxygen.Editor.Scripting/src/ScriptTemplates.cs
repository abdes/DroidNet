// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;

namespace Oxygen.Editor.Scripting;

/// <summary>The text of a new script, and the template New Script creates it from.</summary>
public static class ScriptTemplates
{
    /// <summary>The text of a new script: one example parameter and the two most common hooks.</summary>
    public const string New = """
        local Script = oxygen.script {
          params = {
            speed = oxygen.param.float { default = 1, min = 0, unit = "m/s",
              description = "How fast the node moves" },
          },
        }

        function Script:on_start()
        end

        function Script:on_gameplay(dt)
        end

        return Script

        """;

    /// <summary>Gets the template of New Script: a Lua file in <c>Scripts</c>, colliding with a Luau file of the same name.</summary>
    public static AuthoredAssetTemplate Asset { get; } = new()
    {
        Folder = "Scripts",
        DefaultName = "NewScript",
        Suffixes = [AssetFileTypes.LuaScript, AssetFileTypes.LuauScript],
        Content = static _ => New,
    };
}
