// Built-in script functions of Caribbean Legend's scripts (they declare them with `native`), implemented in
// natives_ext.cpp. Included into FUNCTION_CODE and IntFuncTable (internal_functions.cpp) and by natives_ext.cpp, each
// defining STORM_NATIVE(id, name, arguments, return type, varargs); the order is the order of the ids.

STORM_NATIVE(FUNC_X_CEILF, "ceilf", 1, VAR_FLOAT, false)
STORM_NATIVE(FUNC_X_HASH, "hash", 1, VAR_INTEGER, false)
STORM_NATIVE(FUNC_X_HRAND, "hrand", 1, VAR_INTEGER, true)
STORM_NATIVE(FUNC_X_SET_GLOBAL_SEED, "SetGlobalSeed", 1, TVOID, false)
STORM_NATIVE(FUNC_X_ASSERT, "assert", 1, TVOID, true)
STORM_NATIVE(FUNC_X_SPRINTF, "Sprintf", 1, VAR_STRING, true)
STORM_NATIVE(FUNC_X_ARRAY_SORT, "ArraySort", 1, TVOID, true)
STORM_NATIVE(FUNC_X_ARRAY_SHUFFLE, "ArrayShuffle", 1, TVOID, false)
STORM_NATIVE(FUNC_X_SMART_SORT, "SmartSort", 6, VAR_REFERENCE, true)
STORM_NATIVE(FUNC_X_SMART_SORT_TABLE, "SmartSortTable", 6, VAR_REFERENCE, true)
STORM_NATIVE(FUNC_X_CLEAR_POST_EVENTS_FOR_EVENT, "ClearPostEventsForEvent", 1, TVOID, false)
STORM_NATIVE(FUNC_X_POST_EVENT_NO_TIME_SCALE, "PostEventNoTimeScale", 2, TVOID, true)
STORM_NATIVE(FUNC_X_SET_EVENT_STATIC_HANDLER, "SetEventStaticHandler", 3, TVOID, false)
STORM_NATIVE(FUNC_X_COLLECT_CALL_STACK, "CollectCallStack", 0, TVOID, false)
STORM_NATIVE(FUNC_X_GET_CALLER_NAME, "GetCallerName", 0, VAR_STRING, false)
STORM_NATIVE(FUNC_X_GET_RDELTA_TIME, "GetRDeltaTime", 0, VAR_FLOAT, false)
STORM_NATIVE(FUNC_X_GET_LAST_COMMIT, "GetLastCommit", 0, VAR_STRING, false)
STORM_NATIVE(FUNC_X_ATTR_RENAME, "attrrename", 2, VAR_INTEGER, false)
STORM_NATIVE(FUNC_X_APPEND_ATTRIBUTES, "AppendAttributes", 2, TVOID, false)
