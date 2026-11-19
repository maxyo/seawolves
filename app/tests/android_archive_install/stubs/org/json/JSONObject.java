package org.json;

import java.util.LinkedHashMap;
import java.util.Map;

/** Minimal host stand-in for Android's org.json (the subset the app uses); not a test of Android parsing. */
public class JSONObject {
    final Map<String, Object> values;

    public JSONObject() {
        values = new LinkedHashMap<>();
    }

    public JSONObject(String text) throws JSONException {
        Object value = new Parser(text).document();
        if (!(value instanceof JSONObject)) throw new JSONException("Not an object");
        values = ((JSONObject) value).values;
    }

    JSONObject(Map<String, Object> values) {
        this.values = values;
    }

    public boolean has(String key) {
        return values.containsKey(key);
    }

    public Object get(String key) throws JSONException {
        if (!values.containsKey(key)) throw new JSONException("Missing field: " + key);
        return values.get(key);
    }

    public String getString(String key) throws JSONException {
        Object value = get(key);
        if (!(value instanceof String)) throw new JSONException("Expected string: " + key);
        return (String) value;
    }

    public String optString(String key) {
        Object value = values.get(key);
        return value instanceof String ? (String) value : "";
    }

    public long getLong(String key) throws JSONException {
        Object value = get(key);
        if (!(value instanceof Number)) throw new JSONException("Expected number: " + key);
        return ((Number) value).longValue();
    }

    public int getInt(String key) throws JSONException {
        return (int) getLong(key);
    }

    public double getDouble(String key) throws JSONException {
        Object value = get(key);
        if (!(value instanceof Number)) throw new JSONException("Expected number: " + key);
        return ((Number) value).doubleValue();
    }

    public int optInt(String key) {
        Object value = values.get(key);
        return value instanceof Number ? ((Number) value).intValue() : 0;
    }

    public boolean optBoolean(String key) {
        return Boolean.TRUE.equals(values.get(key));
    }

    public boolean getBoolean(String key) throws JSONException {
        Object value = get(key);
        if (!(value instanceof Boolean)) throw new JSONException("Expected boolean: " + key);
        return (Boolean) value;
    }

    public JSONObject getJSONObject(String key) throws JSONException {
        Object value = get(key);
        if (!(value instanceof JSONObject)) throw new JSONException("Expected object: " + key);
        return (JSONObject) value;
    }

    public JSONObject optJSONObject(String key) {
        Object value = values.get(key);
        return value instanceof JSONObject ? (JSONObject) value : null;
    }

    public JSONArray getJSONArray(String key) throws JSONException {
        Object value = get(key);
        if (!(value instanceof JSONArray)) throw new JSONException("Expected array: " + key);
        return (JSONArray) value;
    }

    public JSONArray optJSONArray(String key) {
        Object value = values.get(key);
        return value instanceof JSONArray ? (JSONArray) value : null;
    }

    public JSONObject put(String key, Object value) throws JSONException {
        values.put(key, value);
        return this;
    }

    static final class Parser {
        private final String text;
        private int at;

        Parser(String text) {
            this.text = text;
        }

        Object document() throws JSONException {
            Object v = value();
            space();
            if (at != text.length()) throw new JSONException("Trailing data");
            return v;
        }

        private void space() {
            while (at < text.length() && Character.isWhitespace(text.charAt(at))) at++;
        }

        private Object value() throws JSONException {
            space();
            if (at >= text.length()) throw new JSONException("Unexpected end");
            char c = text.charAt(at);
            if (c == '{') {
                at++;
                Map<String, Object> map = new LinkedHashMap<>();
                space();
                if (text.charAt(at) == '}') {
                    at++;
                    return new JSONObject(map);
                }
                while (true) {
                    space();
                    Object key = value();
                    if (!(key instanceof String)) throw new JSONException("Key");
                    space();
                    expect(':');
                    map.put((String) key, value());
                    space();
                    if (text.charAt(at) == ',') {
                        at++;
                        continue;
                    }
                    expect('}');
                    return new JSONObject(map);
                }
            }
            if (c == '[') {
                at++;
                java.util.List<Object> list = new java.util.ArrayList<>();
                space();
                if (text.charAt(at) == ']') {
                    at++;
                    return new JSONArray(list);
                }
                while (true) {
                    list.add(value());
                    space();
                    if (text.charAt(at) == ',') {
                        at++;
                        continue;
                    }
                    expect(']');
                    return new JSONArray(list);
                }
            }
            if (c == '"') {
                at++;
                StringBuilder out = new StringBuilder();
                while (true) {
                    char d = text.charAt(at++);
                    if (d == '"') return out.toString();
                    if (d == '\\') {
                        char e = text.charAt(at++);
                        if (e == 'u') {
                            out.append((char) Integer.parseInt(text.substring(at, at + 4), 16));
                            at += 4;
                        } else
                            out.append(
                                    e == 'n'
                                            ? '\n'
                                            : e == 't'
                                                    ? '\t'
                                                    : e == 'r'
                                                            ? '\r'
                                                            : e == 'b'
                                                                    ? '\b'
                                                                    : e == 'f' ? '\f' : e);
                    } else out.append(d);
                }
            }
            if (text.startsWith("true", at)) {
                at += 4;
                return Boolean.TRUE;
            }
            if (text.startsWith("false", at)) {
                at += 5;
                return Boolean.FALSE;
            }
            if (text.startsWith("null", at)) {
                at += 4;
                return null;
            }
            int start = at;
            while (at < text.length() && "+-0123456789.eE".indexOf(text.charAt(at)) >= 0) at++;
            if (start == at) throw new JSONException("Unexpected character " + c);
            String number = text.substring(start, at);
            return number.contains(".") || number.contains("e") || number.contains("E")
                    ? (Object) Double.parseDouble(number)
                    : (Object) Long.parseLong(number);
        }

        private void expect(char c) throws JSONException {
            if (at >= text.length() || text.charAt(at) != c)
                throw new JSONException("Expected " + c);
            at++;
        }
    }
}
