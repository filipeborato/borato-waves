# Issue tracker: Local Markdown

Issues and specs for this repository live under `.scratch/`.

## Conventions

- Keep one initiative per directory: `.scratch/<feature-slug>/`.
- Use `spec.md` in that directory when an initiative has a specification.
- Keep one implementation ticket per file in `issues/<NN>-<slug>.md`, numbered from `01` in dependency order. Do not combine tickets into one file.
- Record `Status:` and `Blocked by:` near the top of each ticket. Use the roles in [triage-labels.md](triage-labels.md) for triage status.
- Append discussion under a `## Comments` heading in the relevant ticket.

When a skill says to publish a ticket, create its file in the initiative's `issues/` directory. When asked to fetch one, read the referenced file. A ticket is ready to start when all tickets named in `Blocked by:` are complete.
