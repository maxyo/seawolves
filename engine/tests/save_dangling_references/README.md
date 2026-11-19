# Saving script references to freed memory

`aref` and `ref` variables are plain pointers; the script language does not clear them when
their target is deleted (`DeleteAttribute`, a rebuilt branch, a resized array, a reloaded
segment). A save writes every global variable, so it meets such pointers even when no script
would use them again. Before the fix this crashed on save (`SIGSEGV` in
`COMPILER::SaveVariable`, in the inlined `TraceARoot` walk from the freed target of the global
`aref reload_locator_ref` (reload.c: the last reload locator used) up through its parents).

An aref of a deleted attribute is known as such (`ATTRIBUTES::Handle`): it is saved empty with one warning and
cleared, so the next save is silent (reload.c leaves `reload_locator_ref` so after every quest reload). The check builds a variable table with live, deleted, local and chained references, deletes
the `reload` branch as a script does, and writes the variables with the production
`COMPILER::SaveVariable` under AddressSanitizer: nothing freed is read, each unsaveable
reference is reported by name and written empty, and the save loads with every live reference
intact (`save_references.hpp`).
