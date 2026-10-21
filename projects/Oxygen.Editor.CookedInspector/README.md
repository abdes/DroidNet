# Oxygen.Editor.CookedInspector

The Cooked Inspector panel: a read-only report of the cooked output of one
scope (a project, a folder or an asset), with its assets, files and issues, and
the information of the asset it inspects.

`CookedInspectorModule` contributes the panel (`inspect`) to each workspace
window. `CookedInspections` implements the Shell's `ICookedInspections` port:
a request from another part (a cook run's Inspect, the Content Explorer's
inspect action) sets the panel's scope and brings it forward, so those parts
reference neither the panel nor this module. The panel's asset links show the
asset in the Content Explorer through the Shell's `IAssetReveal` port.

The panel presents the report only; reading and validating cooked output belong
to ContentPipeline.
