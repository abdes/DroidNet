// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config.Detail;

using System.Diagnostics;
using System.IO.Abstractions;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Options;

internal sealed class WritableOptions<T>(
    IOptionsMonitor<T> options,
    IConfigurationRoot configuration,
    string sectionName,
    string filePath,
    IFileSystem fs) : IWritableOptions<T>
    where T : class, new()
{
    public T Value => options.CurrentValue;

    public void Update(Action<T> applyChanges)
    {
        // By default, the serialized JSON has the numeric value of the enums.
        // Using the custom converter will make it serialize the enum name
        // instead, using camel case which is the default case supported by the
        // serializer.
        var jsonSerializerOptions = new JsonSerializerOptions
        {
            WriteIndented = true,
            Converters = { new JsonStringEnumConverter(JsonNamingPolicy.CamelCase) },
        };

        try
        {
            var jObject = fs.File.Exists(filePath)
                ? JsonSerializer.Deserialize<JsonObject>(fs.File.ReadAllText(filePath)) ?? []
                : [];
            var sectionObject = jObject.TryGetPropertyValue(sectionName, out var section)
                ? JsonSerializer.Deserialize<T>(section!.ToString(), jsonSerializerOptions)
                : this.Value;

            applyChanges(sectionObject!);

            jObject[sectionName] = JsonNode.Parse(JsonSerializer.Serialize(sectionObject, jsonSerializerOptions));
            var configText = JsonSerializer.Serialize(jObject, jsonSerializerOptions);
            fs.File.WriteAllText(filePath, configText);
        }
        catch (Exception ex)
        {
            Debug.WriteLine(ex);
            throw;
        }

        // Reload the configuration
        configuration.Reload();
    }

    public T Get(string name) => options.Get(name);
}
