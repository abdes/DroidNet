// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

using System.Diagnostics.CodeAnalysis;
using System.IO.Abstractions;
using System.Text.Json;
using System.Text.Json.Nodes;
using FluentAssertions;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Testably.Abstractions.Testing;
using Testably.Abstractions.Testing.Initializer;

[TestClass]
[ExcludeFromCodeCoverage]
public class WritableOptionsTests
{
    private const string SectionName = "TestSection";
    private const string OtherSectionName = "OtherSection";

    private readonly string configFilePath = "test.json";

    [TestMethod]
    public void ConfigureWritable_Registers_WritableOptions()
    {
        // Arrange
        var builder = Host.CreateDefaultBuilder();
        _ = builder.ConfigureServices(sc => sc.AddSingleton<IFileSystem>(new MockFileSystem()));

        // The following should normally be done by configurating the builder to add the config file, but for testing purposes,
        // we don't want the builder to access the filesystem.
        var configuration = new ConfigurationBuilder().Build();
        var section = configuration.GetSection(SectionName);

        // Act
        _ = builder.ConfigureServices(sc => sc.ConfigureWritable<MyOptions>(section, this.configFilePath));

        // Assert
        var host = builder.Build();
        var writableOptions = host.Services.GetRequiredService<IWritableOptions<MyOptions>>();
        _ = writableOptions.Should().NotBeNull();
    }

    [TestMethod]
    public void WritableOptions_PersistsChanges()
    {
        // Arrange
        const string initialValue = "initial";
        const string updatedValue = "updated";
        var fileSystem = new MockFileSystem();

        var builder = Host.CreateDefaultBuilder();
        _ = builder.ConfigureServices(
            sc =>
            {
                _ = fileSystem.Initialize()
                    .With(
                        new FileDescription(
                            this.configFilePath,
                            CreateInitialConfig(initialValue)));
                _ = sc.AddSingleton<IFileSystem>(fileSystem);
            });

        // The following should normally be done by configurating the builder to add the config file, but for testing purposes,
        // we don't want the builder to access the filesystem.
        var configuration = new ConfigurationBuilder().Build();
        var section = configuration.GetSection(SectionName);

        // Act
        _ = builder.ConfigureServices(sc => sc.ConfigureWritable<MyOptions>(section, this.configFilePath));
        var host = builder.Build();
        var writableOptions = host.Services.GetRequiredService<IWritableOptions<MyOptions>>();
        writableOptions.Update(options => options.MyProperty = updatedValue);

        // Assert
        var jObject = JsonSerializer.Deserialize<JsonObject>(fileSystem.File.ReadAllText(this.configFilePath));
        _ = jObject.Should().NotBeNull();
        var updatedOptions = GetSection(jObject, SectionName);
        _ = updatedOptions.Should().BeEquivalentTo(new MyOptions() { MyProperty = updatedValue });

        // The other section should be preserved
        var otherOptions = GetSection(jObject, OtherSectionName);
        _ = otherOptions.Should().BeEquivalentTo(new MyOptions() { MyProperty = initialValue });
    }

    private static MyOptions? GetSection(JsonObject? jObject, string sectionName)
        => jObject!.TryGetPropertyValue(sectionName, out var section)
            ? JsonSerializer.Deserialize<MyOptions>(section!.ToString())
            : null;

    private static string CreateInitialConfig(string initialValue)
    {
        var json = new JsonObject
        {
            [SectionName] = JsonNode.Parse(
                JsonSerializer.Serialize(
                    new MyOptions()
                    {
                        MyProperty = initialValue,
                    })),
            [OtherSectionName] = JsonNode.Parse(
                JsonSerializer.Serialize(
                    new MyOptions()
                    {
                        MyProperty = initialValue,
                    })),
        };
        return JsonSerializer.Serialize(json);
    }

    private sealed class MyOptions
    {
        public string? MyProperty { get; set; }
    }
}
