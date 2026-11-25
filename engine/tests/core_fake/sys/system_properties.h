#pragma once
// Android's system properties for engine code built for a host check: none is set.
#define PROP_VALUE_MAX 92
inline int __system_property_get(const char *, char *value)
{
    value[0] = '\0';
    return 0;
}
