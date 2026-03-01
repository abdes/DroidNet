
• With a corresponding JSON schema

  - Import manifest schema source is `src/Oxygen/Cooker/Import/Schemas/import-manifest.schema.json`
    (Draft-07). The C++ header `ImportManifest_schema.h` is generated at build time by
    `cmake/JsonSchemaHelpers.cmake`.
  - Manifest JSON is parsed and schema-validated in ImportManifest.cpp:63, ImportManifest.cpp:64,
    ImportManifest.cpp:71, ImportManifest.cpp:555.
  - Batch import entrypoints that consume this schema-validated manifest: BatchCommand.cpp:365,
    BatchCommand.cpp:429.

  JSON used without a dedicated JSON schema

  - Inline sidecar JSON (inline_bindings_json) is parsed/normalized with ad-hoc checks (array or
    object with bindings), not schema-validated: ScriptImportRequestBuilder.cpp:88,
    ScriptImportRequestBuilder.cpp:94, ScriptImportRequestBuilder.cpp:226.
  - Sidecar document payload is parsed and validated in code (field-by-field), no JSON schema file:
    ScriptingSidecarImportPipeline.cpp:309, ScriptingSidecarImportPipeline.cpp:323,
    ScriptingSidecarImportPipeline.cpp:337.
  - Sidecar source loader accepts inline JSON bytes or file bytes before parse (no schema stage):
    ScriptingSidecarImportJob.cpp:133, ScriptingSidecarImportJob.cpp:137,
    ScriptingSidecarImportJob.cpp:169.
  - ImportTool JSON report output is generated/written without a report schema:
    ImportRunner.cpp:63, ImportRunner.cpp:271, BatchCommand.cpp:234, BatchCommand.cpp:1123,
    ReportJson.cpp:154.
