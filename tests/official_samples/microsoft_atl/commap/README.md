# Original Microsoft COMMap MIDL input (source unmodified)

This folder contains the original `COMMap.idl` from
`microsoft/VCSamples@9e1d4475555b76a17a3568369867f1d7b6cc6126`,
path `VC2010Samples/ATL/General/commap/COMMap.idl`. The exact Git blob SHA
is `dcf19ff69001c6f2f242d15c917a5598b03d70ba`, verified before CMake
generates anything. `.gitattributes` disables checkout CRLF conversion for
the original IDL. The upstream MIT license is retained in
`../MICROSOFT_LICENSE.txt`.

On native Windows, the selected Windows SDK `midl.exe` is required to
generate real `COMMap.h`, `COMMap_i.c` and `COMMap.tlb` from these
unchanged bytes. The generated IID source and interface header are compiled
against the OpenATL qualification target, and a native test loads the actual
binary TypeLib with `REGKIND_NONE`, verifying its original LIBID, IIDs,
version and name DISPIDs without registry writes.

This qualifies the **original MIDL/TypeLib input**, not full COMMap
application source compatibility. The original `Outer.h` also requires
aggregation, cached tear-offs and `IDispatchImpl`; MIDL admission does not
qualify those missing pieces. Linux/macOS cross-hosts do not have the Windows
SDK MIDL generator and therefore only verify the pinned original source and
continue cross-compiling unrelated OpenATL targets. They are not counted as
having generated a Windows TypeLib.
