# Signing Certificates

All Windows app packages must be digitally signed before they can be deployed.
While using Visual Studio 2022, it's easy to package and publish an app, but
that process does not go well with automation.

The scripts in this folder help to generate a signing certificate and create the
secret we need for the GitHub workflows.

## Generate Test Certificate

Use this script to create a test certificate. This certificate can be used
locally during testing to sign app packages with the MSBuild command to create
them. This scenario si an alternate way to do the dev/test lifecycle without the
Visual Studio UI.

Most importantly, the generated certificate is used as a secret in the GitHub CI
workflow as explained [below](#github-secret)

## GitHub Secret

Use this script creates a BASE64 string of the certificate. This is utilized by
the workflow action for [github](https://docs.github.com/en/actions/security-guides/using-secrets-in-github-actions).
