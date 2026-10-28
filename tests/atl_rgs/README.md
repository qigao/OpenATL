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

The test program compiles these pinned files as immutable wide literals on
Windows through a tiny generator, and accepts their **read-only syntax and
projection** with exact node/value counts. This is **not** registry execution
or proof of full ATL RGS compatibility. The `HKCR` keyword is a symbolic
input root, not authorization to write to real HKCR/HKLM.

`ForceRemove` and `NoRemove` remain parsed directives only. No recursive
deletion, hidden elevation, external registry mutation or COM initialization
occurs in this grammar-only milestone. Subsequent registry execution requires
a caller-selected root, owner-qualified value/child verification, fail-closed
unsupported constructs and native isolated HKCU tests.
