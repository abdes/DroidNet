# M07B project closure during cooking

Date: 2026-09-15

Two packaged native integration cases close the active project while a material
cook is queued or after native output has completed but before publication.
The final publication/lifetime group passes **17/17**.

Both cases use the real project context, automatic scheduler, saved material
service, native cooker, publication service and running engine. The second case
holds the publication service's asynchronous preview factory at a controlled
boundary; it returns the same workspace preview adapter used in production.

After saving a changed material, the tests close the project, unregister its
preview and shut down the engine. They verify:

- The old cook completes as Cancelled.
- Every file under the published `.cooked` tree retains its original hash.
- Catalog work drains and exclusive output ownership can be acquired, proving
  the runtime and status readers have released their leases.
- Reopening validates the committed receipt, starts a different native engine
  run and restores the saved scene using the previous cooked material.
- An explicit retry publishes the newer saved material and updates both native
  scene consumers.

The cases exercise service-level project and runtime closure; existing document
host, viewport and shutdown tests cover the surrounding UI/resource teardown.

Evidence: `artifacts/TestResults/m07b-project-closure-final.trx`.
Build: `artifacts/m07b-project-closure-clean-build.log`.
All changed files have no analyzer or IDE diagnostics.
