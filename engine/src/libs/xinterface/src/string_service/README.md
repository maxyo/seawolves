# String service

`STRSERVICE` holds the texts of the game language: the common strings (`common.ini` of the language, what
`XI_ConvertString` reads) and the text files the scripts open by name (`LanguageOpenFile`; `user_string_files.hpp`
keeps the recently closed ones parsed). `LanguageSetLanguage` switches all of them and tells the compiler, so that
scripts compiled afterwards include that language's `%language%` files.

## Text templates

Games whose texts are templates (Caribbean Legend) expand them with the natives `DLGO(text[, context])`,
`DLG_Convert(key, file[, context])`, `DLG_ConvertE` (a missing key gives `""`) and `StringFromKey(key, args...)`. The
natives run in core (`natives_texts.cpp`), which answers what a template reads from the running scripts; the language
is `text_templates.cpp`:

| Construct | Result |
| --- | --- |
| `$name`, `$name()` | a built-in, or else the first script function among `name`, `DLG_Name`, `RT_Name`, `Name`, called with the arguments as text |
| `$name(a\|b)` | the arguments are split at `\|` of this level and expanded before the call; choices (`$rand`, `$manWoman`, `$goodBad` ...) expand only the chosen one |
| `#spath#`, `#dpath#`, `#fpath#`, `#f.Npath#` | an attribute of the context as text, integer, float, float with N decimals |
| `\x` | the character `x`; `\n` stays for the text renderer |
| U+21B4 | lower-cases the first letter of what follows |
| `$dialogBreak()` | kept for the dialogue window |

The player is `pchar`, the dialogue partner `CharacterRef`, the context the object passed to the native. `$key(k)`
looks in the common strings and then in the file being expanded; `$key`, `$ctx` and `$link` values are templates too.
`StringFromKey` reads the files under `Localization_Assets` and replaces each `@<...>` with the next argument. What
cannot be expanded stays in the text as written and is logged once.

`GetRichTextWidth(text, font, scale)` measures the rich-text markup of `../rich_text.hpp`.
