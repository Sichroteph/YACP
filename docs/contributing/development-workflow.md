---
title: Development Workflow
parent: Contributing
nav_order: 3
---

# Development Workflow

This page defines a practical local workflow for maintaining a personal fork.

## 1) Fork and create a focused branch

- Fork the repository to your own GitHub account
- Clone your fork locally and add the upstream repository if needed
- Enable repo hooks once per clone: `git config core.hooksPath .githooks && chmod +x .githooks/pre-commit`

- Branch from `main` for focused local work
- Keep each commit focused on one fix or feature area

## 2) Implement with scope in mind

- Confirm your idea is in project scope: [SCOPE.md](../../SCOPE.md)
- Prefer incremental changes over broad refactors

## 3) Run local checks

```sh
./bin/clang-format-fix
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run -e simulator
pio run -e default
```

CI enforces formatting, static analysis, and the primary firmware build.
Use clang-format 21+ locally to match CI.
If `clang-format` is missing or too old locally, see [Getting Started](./getting-started.md).
Run plain `pio run` before larger changes to build the release variants (`tiny` and `xlarge`).

## 4) Record the change

- Use a semantic title (example: `fix: avoid crash when opening malformed epub`)
- Describe the problem, approach, and any tradeoffs in the commit or local release notes
- Include reproduction and verification steps for bug fixes

## 5) Review

- Review the diff for unrelated changes
- Check resource costs on the ESP32-C3
- Record the concrete hardware path used for validation

YACP does not accept pull requests. See [GOVERNANCE.md](../../GOVERNANCE.md).
