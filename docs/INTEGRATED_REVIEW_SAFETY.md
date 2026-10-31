# Combined native WTL security and ATL ownership acceptance

The existing Windows x64/ARM64 MSVC and llvm-mingw suites qualify source-owned
ATL/WTL behavior on the exact checkout. Required behavior includes native UI/COM,
CFG/W^X thunks, dynamic-code denial, DLL admission/leases/unload, string and object
ownership, and failure cleanup. Each native suite repeats five times; real
Windows ARM64 artifacts retain exact SHA256/PE/resource provenance.

Hardware HSP enforcement and real printer/PrintDlgEx device probes are outside
CI, including manual GitHub Actions workflows, by owner decision (2026-10-31).
They are not merge or feature-completion gates. CFG/CET image checks and
printing COM callback/site tests remain required and do not depend on physical
hardware. The explicit local diagnostic modes remain available.

Passing the native subset does not claim arbitrary uncooperative DLL unload,
complete ATL compatibility, printer readiness or hardware HSP enforcement.
See [ACCEPTANCE.md](ACCEPTANCE.md) and [ATL_COMPATIBILITY.md](ATL_COMPATIBILITY.md)
for the current supported surface and implementation backlog.
