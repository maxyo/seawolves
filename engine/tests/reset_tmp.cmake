# The fixture of every check (tests/CMakeLists.txt tmp.reset): ROOT emptied, then one empty directory per check
# (the paths in LIST), so a run never sees what an earlier run left.
file(REMOVE_RECURSE "${ROOT}")
file(STRINGS "${LIST}" dirs)
file(MAKE_DIRECTORY ${dirs})
