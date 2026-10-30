## What and why

## How it was tested
<!-- Commands run and results; platforms built; host(s) tried, if any. -->

## Checklist
- [ ] Builds warning-free with `-DKRAFTONIC_WARNINGS_AS_ERRORS=ON`
- [ ] `ctest` passes; new DSP behaviour has tests
- [ ] No allocation, locks, I/O or logging on the audio thread
- [ ] No parameter IDs/keys renamed or reused; plug-in IDs unchanged
- [ ] All code/presets/assets are original or properly licensed (see THIRD_PARTY_NOTICES.md and TRADEMARKS.md)
- [ ] All commits are signed off (`git commit -s`, Developer Certificate of Origin)
- [ ] New samples and presets follow the content rules in `CONTRIBUTING.md` (own recording, CC0 or CC BY; source pinned and credited)
