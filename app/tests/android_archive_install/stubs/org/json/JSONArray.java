package org.json;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

/** Minimal host stand-in for Android's org.json.JSONArray. */
public class JSONArray {
    private final List<Object> values;

    public JSONArray() {
        values = new ArrayList<>();
    }

    public JSONArray(Collection<?> values) {
        this.values = new ArrayList<>(values);
    }

    JSONArray(List<Object> values) {
        this.values = values;
    }

    public int length() {
        return values.size();
    }

    public Object get(int index) throws JSONException {
        if (index < 0 || index >= values.size()) throw new JSONException("Invalid index");
        return values.get(index);
    }

    public String getString(int index) throws JSONException {
        Object value = get(index);
        if (!(value instanceof String)) throw new JSONException("Expected string");
        return (String) value;
    }

    public JSONObject getJSONObject(int index) throws JSONException {
        Object value = get(index);
        if (!(value instanceof JSONObject)) throw new JSONException("Expected object");
        return (JSONObject) value;
    }
}
