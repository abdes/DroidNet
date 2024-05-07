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
    string section,
    string filePath,
    IFileSystem fs) : IWritableOptions<T>
    where T : class, new()
{
    private readonly IConfigurationRoot configuration = configuration;
    private readonly string filePath = filePath;
    private readonly IFileSystem fs = fs;
    private readonly IOptionsMonitor<T> options = options;
    private readonly string sectionName = section;

    public T Value => this.options.CurrentValue;

    public void Update(Action<T> applyChanges)
    {
        // By default the serialized JSON has the numeric value of the enums.
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
            var jObject = this.fs.File.Exists(this.filePath)
                ? JsonSerializer.Deserialize<JsonObject>(this.fs.File.ReadAllText(this.filePath)) ?? new JsonObject()
                : new JsonObject();
            var sectionObject = jObject.TryGetPropertyValue(this.sectionName, out var section)
                ? JsonSerializer.Deserialize<T>(section!.ToString(), jsonSerializerOptions)
                : this.Value;

            applyChanges(sectionObject!);

            jObject[this.sectionName] = JsonNode.Parse(JsonSerializer.Serialize(sectionObject, jsonSerializerOptions));
            var configText = JsonSerializer.Serialize(jObject, jsonSerializerOptions);
            this.fs.File.WriteAllText(this.filePath, configText);
        }
        catch (Exception ex)
        {
            Debug.WriteLine(ex);
            throw;
        }

        // Reload the configuration
        this.configuration.Reload();
    }

    public T Get(string name) => this.options.Get(name);
}
