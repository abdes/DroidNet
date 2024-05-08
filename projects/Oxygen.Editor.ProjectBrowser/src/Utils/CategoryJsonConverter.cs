// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Utils;

using System.Text.Json;
using System.Text.Json.Serialization;
using Oxygen.Editor.ProjectBrowser.Config;

public sealed class CategoryJsonConverter : JsonConverter<ProjectCategory>
{
    private readonly ProjectBrowserSettings settings;

    public CategoryJsonConverter(ProjectBrowserSettings settings)
        => this.settings = settings;

    public override ProjectCategory Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
    {
        if (reader.TokenType != JsonTokenType.String)
        {
            throw new JsonException($"Expecting a string token for the category id, but got `{reader.TokenType}`");
        }

        var categoryId = reader.GetString();
        var category = this.settings.GetProjectCategoryById(categoryId!);
        return category ?? throw new JsonException($"Unknown category id '{categoryId}'.");
    }

    public override void Write(Utf8JsonWriter writer, ProjectCategory value, JsonSerializerOptions options)
        => writer.WriteStringValue(value.Id);
}
