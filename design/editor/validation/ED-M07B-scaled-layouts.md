# ED-M07B scaled inspector and Cooking layouts

Validated 2026-09-14 in the existing packaged Release WorldEditor UI test host.
`ScaledXamlHost` uses the supported `DesktopWindowXamlSource.SiteBridge.OverrideScale`
and asserts the resulting `XamlRoot.RasterizationScale` equals 1, 1.5 or 2.
Desktop display settings are unchanged. See the [Microsoft API contract](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.content.desktopsitebridge.overridescale?view=windows-app-sdk-1.8).

| Surface | Logical size and theme | Checks at each scale |
| --- | --- | --- |
| Single-node inspector | 360 x 620 dark; 280 x 320 light | Compact header, visible All icon and tooltip, original property rows, Geometry filter and All reset, unchanged dirty/history, scroll to final property |
| Multi-node inspector | 360 x 160 dark; 540 x 480 light | Bounded component rows, visible All icon, applicable sections, selection/reset and scrolling |
| Cooking | 960 x 340 dark; 360 x 340 light | Compact run/recovery header, semantic issues, Show all, expandable output/assets, no inner scrolling, outer scroll reaches final asset |

Result: **18/18 passed**. Render captures at the initial and final scroll offsets
were attached to each case; visual review confirms the compact controls and
reachable final rows. Narrow property values retain the approved clipping behavior.

Evidence:

- `artifacts/TestResults/m07b-scaled-layout-final.trx`
- Captures: `artifacts/TestResults/abdes_GIGA_2026-09-14_23_54_32/In/`
- Build/analyzers: `artifacts/m07b-scaled-ui-clean-build.log`; no unsuppressed
  diagnostics at any severity in the three changed test files.

This closes the inspector's remaining scale requirement alongside its existing
selection/lifetime/edit coverage. Import dialogs, browser command journeys and
the full viewport qualification retain their own validation gates.
