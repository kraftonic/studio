# Security policy

## Reporting a vulnerability

Please report security problems **privately** through GitHub: **[Report a vulnerability](https://github.com/kraftonic/studio/security/advisories/new)** (the repository's Security tab → "Report a vulnerability"). Only the maintainers can see the report. Don't open a public issue, pull request or discussion about them.

Include what you can of:
- the affected product, version or commit, and operating system;
- what the problem is and what an attacker could do with it;
- steps or a file that reproduces it.

## In scope

- the modules and the starter plug-ins, including how they load presets, saved state and sample files;
- the installers and packages built from [`packaging/`](packaging/);
- the scripts in [`scripts/`](scripts/).

Third-party code we include (listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)) is in scope where our use of it is the problem; otherwise please also report it to its own project. Host or DAW crashes that aren't caused by Kraftonic code are out of scope.

## What to expect

Kraftonic is maintained by a small team of volunteers, so we can't promise fixed response times. We will:
- acknowledge your report as soon as we can;
- keep you informed while we investigate and fix it;
- credit you for the report, unless you prefer not to be named.

Please give us reasonable time to fix the problem before you disclose it publicly.
