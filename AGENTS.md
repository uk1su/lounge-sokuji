# Working on Lounge Sokuji / QuickScore

This is a Windows C++17 / Qt6 OBS plugin. Start with README.md and docs/HANDOFF.md.

- Active recognition: src/ui/main-dock.cpp -> src/core/jev-api-reader.cpp -> TypeSafe Jev 1.14 HTTP API. Legacy LocalReader and Python workers are not the active API path. Do not accidentally restore the GPU startup dependency.
- Preserve the existing score line, UTF-8 score.txt without a trailing newline, and Discord's explicit send button behavior.
- Result detection is independent of name recognition. Preserve stable frames, the initial non-result guard, one automatic request per result cycle, and cancellation of late replies.
- Never fill unknown teams from team capacity. Keep the unknown choice and all confidence / probability / margin checks. Team sizes and the 82-point total validate a result; they are not evidence for missing names.
- A team's first grapheme identifies it; the user's team is the first configured tag, never a hardcoded ITF. Preserve NFKC / case / kana identity rules and distinguish voiced kana.
- Only a manually confirmed result may teach the CPU template bank. API answers alone must not train it.
- API keys and PATs stay outside Git. Use Windows Credential Manager or process-scoped environment variables. Never print secrets, put them in remote URLs, or commit screenshots, session history, settings, or raw capture reports.
- Tests are documented in docs/TESTING.md. Mock HTTP tests do not prove live recognition accuracy. Real API calls cost money; use the session's authorization and a bounded fixture set, not an unbounded retry loop.
- Before replacing an installed DLL, verify OBS is stopped. If it is running, finish the tested package first and coordinate closure with the user unless the session already authorizes stopping it. Back up, install, compare hashes, then verify the restart log separately from libobs tests.
- Keep README, CHANGELOG, and the handoff status accurate when changing the implementation. Separate an available package from the version currently loaded by OBS.
