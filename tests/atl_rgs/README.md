# Pinned original Microsoft ATL RGS conformance corpus

These four **unchanged** .rgs text fixtures are copied from
[microsoft/VCSamples](https://github.com/microsoft/VCSamples/tree/9e1d4475555b76a17a3568369867f1d7b6cc6126/VC2010Samples/ATL)
at exact commit `9e1d4475555b76a17a3568369867f1d7b6cc6126`, under the upstream MIT license in
`MICROSOFT_LICENSE.txt`. Git blob SHA-1 validates each fixture *byte for byte*
at configure time; no token rewriting, sample API patching, or substituted
RGS source is permitted.

| Fixture | Original relative path from `VC2010Samples/ATL/` | Original Git blob SHA-1 |
|---|---|---|
| Chain.rgs | `General/commap/Chain.rgs` | `30525e92df26595e9bc00015ea970b3ea5d97e04` |
| Outer.rgs | `General/commap/Outer.rgs` | `0ff53ea6aa644a7b96ec6a6f77dc8ab3f1680285` |
| ArrayManager.rgs | `General/atlsafearray/ArrayManager.rgs` | `39bf8a7d2bc75c21f81c74b761ce97c95234c59a` |
| MapCollection.rgs | `General/ATLCollections/MapCollection.rgs` | `b81d8194a2c9020de381c8e9f90eb01b6fa6428f` |

The test program embeds these pinned files as immutable wide literals on
Windows and accepts their **read-only syntax and projection** with exact
node/value counts. Separately, the native COM test now exercises the opt-in
owner-verified executor under a newly GUID-named, isolated HKCU test root.
This is still **not** proof of complete ATL RGS compatibility. Separate
DLL tests now exercise module resource registration. The `HKCR` keyword is a symbolic
input root, not authorization to write to real HKCR/HKLM.

`ForceRemove` and `NoRemove` remain parsed directives in the AST.
Registration execution is an **explicit separate API** requiring an opened
caller-owned HKEY and registration owner tag. `ForceRemove` is *not*
authority to replace or recursively delete existing keys. The native
test's isolated HKCU root is deleted only if newly created by that test;
production execution performs no blind recursive deletion, hidden elevation
or COM initialization. The numeric/named `DECLARE_REGISTRY_RESOURCEID` and
`DECLARE_REGISTRY_RESOURCE` macros load compiled Win32 `REGISTRY` resources
through the module's default context or an explicit installer context.
`CAtlModule::UpdateRegistryFromResourceS` feeds bounded replacement maps into
the same parser: only key/name/string data is substituted, never syntax.
The fixture embeds the unchanged
Microsoft Chain/Outer files as resources and compares their loaded AST to
these pinned text inputs. Real COM classes use two class-matching RGS
resources for native STA/MTA activation, including an actual named resource.
The native TypeLib subset has separate tests; full ATL registrar/resource
language and complete TypeLib semantics remain separate work.
