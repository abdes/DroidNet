// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Templates;

using System.IO.Abstractions;
using FluentAssertions;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Oxygen.Editor.ProjectBrowser.Config;
using Testably.Abstractions.Testing;

[TestClass]
[TestCategory(nameof(LocalTemplatesSource))]
public class LocalTemplatesSourceTests
{
    private const string Settings =
        /*lang=json,strict*/
        """
        {
          "ProjectBrowserSettings": {
            "Categories": [
              {
                "Id": "test-1",
                "Name": "Test Category 1",
                "Description": "Just for testing"
              },
              {
                "Id": "test-2",
                "Name": "Test Category 2",
                "Description": "Just for testing"
              }
            ],
            "BuiltinTemplates": [
              "templates/1/template.json",
              "templates/2/template.json",
              "templates/missing-descriptor-file/template.json",
              "templates/missing-icon-file/template.json",
              "templates/missing-preview-file/template.json"
            ]
          }
        }

        """;

    private readonly string[] descriptors =
    [
        /*lang=json,strict*/
        """
        {
            "Name": "Template 1",
            "Description": "Template 1 Description",
            "Icon": "icon.png",
            "Category": "test-1",
            "PreviewImages": [
            "preview.png"
            ]
        }
        """,
        /*lang=json,strict*/
        """
        {
            "Name": "Template 2",
            "Description": "Template 2 Description",
            "Icon": "icon.png",
            "Category": "test-2",
            "PreviewImages": [
              "preview.png"
            ]
        }
        """,
    ];

    private readonly MockFileSystem fileSystem = new();
    private readonly IHost host;

    public LocalTemplatesSourceTests()
    {
        this.InitializeFileSystem();

        this.host = Host.CreateDefaultBuilder()
            .ConfigureAppConfiguration(
                (_, config) => config.AddJsonStream(this.fileSystem.File.OpenRead("settings.json")))
            .ConfigureServices(
                (context, sc) => _ = sc.AddSingleton<IFileSystem>(this.fileSystem)
                    .AddSingleton(Settings)
                    .AddSingleton<ITemplatesSource, LocalTemplatesSource>()
                    .Configure<ProjectBrowserSettings>(
                        context.Configuration.GetSection(ProjectBrowserSettings.ConfigSectionName)))
            .Build();
    }

    [TestMethod]
    [DataRow("file:///x", true)]
    public void CanLoad_FromFile(string uri, bool canLoad)
    {
        // Arrange
        var sut = this.host.Services.GetRequiredService<ITemplatesSource>();

        // Act
        var can = sut.CanLoad(new Uri(uri));

        // Assert
        _ = can.Should().Be(canLoad);
    }

    [TestMethod]
    public async Task LoadTemplate_Throws_IfNotFromFileAsync()
    {
        // Arrange
        var sut = this.host.Services.GetRequiredService<ITemplatesSource>();

        // Act
        var act = () => sut.LoadTemplateAsync(new Uri("http://xyz"));

        // Assert
        _ = await act.Should().ThrowAsync<ArgumentException>().WithParameterName("fromUri");
    }

    [TestMethod]
    public async Task LoadTemplate_Throws_IfTemplateFileDoesNotExistAsync()
    {
        // Arrange
        var sut = this.host.Services.GetRequiredService<ITemplatesSource>();

        // Act
        var act = ()
            => sut.LoadTemplateAsync(new Uri("file:///templates/missing-descriptor-file/template.json"));

        // Assert
        _ = await act.Should().ThrowAsync<TemplateLoadingException>().WithMessage("*existing*");
    }

    [TestMethod]
    public async Task LoadTemplate_Throws_IfSpecifiedIconFileDoesNotExistAsync()
    {
        // Arrange
        var sut = this.host.Services.GetRequiredService<ITemplatesSource>();

        // Act
        var act = () => sut.LoadTemplateAsync(new Uri("file:///templates/missing-icon-file/template.json"));

        // Assert
        _ = await act.Should().ThrowAsync<TemplateLoadingException>().WithMessage("*icon*");
    }

    [TestMethod]
    public async Task LoadTemplate_Throws_IfSpecifiedPreviewFileDoesNotExistAsync()
    {
        // Arrange
        var sut = this.host.Services.GetRequiredService<ITemplatesSource>();

        // Act
        var act = () => sut.LoadTemplateAsync(new Uri("file:///templates/missing-preview-file/template.json"));

        // Assert
        _ = await act.Should().ThrowAsync<TemplateLoadingException>().WithMessage("*preview*");
    }

    [TestMethod]
    [DataRow("1")]
    [DataRow("2")]
    public async Task LoadTemplate_Succeeds_IfTemplateIsGood(string templateNumber)
    {
        // Arrange
        var sut = this.host.Services.GetRequiredService<ITemplatesSource>();

        // Act
        var template = await sut.LoadTemplateAsync(new Uri($"file:///templates/{templateNumber}/template.json"));

        // Assert
        _ = template.Name.Should().Be($"Template {templateNumber}");
        _ = template.Category.Should().NotBeNull();
        _ = template.Category.Id.Should().Be($"test-{templateNumber}");
        _ = template.Location.Should().Be(this.fileSystem.Path.GetFullPath($"/templates/{templateNumber}"));
    }

    private void InitializeFileSystem() => _ = this.fileSystem.InitializeIn("/")
        .WithFile("settings.json")
        .Which(f => f.HasStringContent(Settings))
        .WithSubdirectory("templates")
        .Initialized(
            d =>
            {
                _ = d.WithSubdirectory("1")
                    .Initialized(
                        sd =>
                        {
                            _ = sd.WithFile("template.json")
                                .Which(f => f.HasStringContent(this.descriptors[0]));
                            _ = sd.WithFile("icon.png");
                            _ = sd.WithFile("preview.png");
                        });
                _ = d.WithSubdirectory("2")
                    .Initialized(
                        sd =>
                        {
                            _ = sd.WithFile("template.json")
                                .Which(f => f.HasStringContent(this.descriptors[1]));
                            _ = sd.WithFile("icon.png");
                            _ = sd.WithFile("preview.png");
                        });
                _ = d.WithSubdirectory("missing-descriptor-file")
                    .Initialized(
                        sd =>
                        {
                            _ = sd.WithFile("icon.png");
                            _ = sd.WithFile("preview.png");
                        });
                _ = d.WithSubdirectory("missing-icon-file")
                    .Initialized(
                        sd =>
                        {
                            _ = sd.WithFile("template.json")
                                .Which(f => f.HasStringContent(this.descriptors[0]));
                            _ = sd.WithFile("preview.png");
                        });
                _ = d.WithSubdirectory("missing-preview-file")
                    .Initialized(
                        sd =>
                        {
                            _ = sd.WithFile("template.json")
                                .Which(f => f.HasStringContent(this.descriptors[0]));
                            _ = sd.WithFile("icon.png");
                        });
            });
}
