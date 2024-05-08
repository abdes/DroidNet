# Project Browser

## Config Data

The project browser uses one configuration section `ProjectTemplatesSettings`
that needs to be loaded and configured in the DI injector. The corresponding
model class is `ProjectTemplateSettings`.

```json
{
  "ProjectTemplatesSettings": {
    "Categories": [
      {
        "Name": "GamesCategory",
        "Description": "GamesCategoryDescription",
        "Location": "Games",
        "IsBuiltIn": true
      },
      {
        "Name": "VisualizationCategory",
        "Description": "VisualizationCategoryDescription",
        "Location": "Visualization",
        "IsBuiltIn": true
      },
      {
        "Name": "ExtendedTemplatesCategory",
        "Description": "ExtendedTemplatesCategoryDescription",
        "Location": "Templates",
        "IsBuiltIn": false
      }
    ]
  }
}
```

## Services

The project browser exposes the following services:

- `ProjectTemplatesService` via the `IProjectTemplatesService` interface.

## Assets

The project browser includes `assets` that need to be copied to the application
program data folder. All assets are under the `Assets` folder.

Note that the config file does not have to be a separate file as long as the
[ProjectTemplatesSettings](#Config-Data) section is loaded by the configuration
from somewhere and is configured into the DI injector.

```xml
  <!-- Copy assets from `ProjectBrowser` project-->
  <ItemGroup>
    <Content Include="..\ProjectBrowser\Assets\**">
      <Link>Assets\%(RecursiveDir)%(Filename)%(Extension)</Link>
      <CopyToOutputDirectory>PreserveNewest</CopyToOutputDirectory>
    </Content>
  </ItemGroup>
```
