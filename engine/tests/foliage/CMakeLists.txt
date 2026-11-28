# The foliage files' readers and the selection (src/libs/foliage) on small invented files, and every foliage file of an
# installed game.
add_subdirectory("${src}/libs/foliage" library)
storm_host_check(foliage OPTIONS ${warnings} ${asan_strict} LIBS storm_foliage nlohmann_json::nlohmann_json CATCH2)
storm_host_check(foliage.corpus SOURCES corpus.cpp OPTIONS ${warnings} LIBS storm_foliage LABELS game TIMEOUT 600)
