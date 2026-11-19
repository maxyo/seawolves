# Application menu translations

Paths and commands in this file are relative to `app/`.

These catalogs contain application-owned UI text, never game scripts, dialogue or
commercial resources. English is the fallback. The native snapshot's `language`
selects the catalog in game; elsewhere the app speaks the installed game's language (`MenuText.appLanguage`: its `language.ini` defaultLanguage), the phone's while no game is installed.

The game's public `resource/ini/texts/language.ini` registers English, Russian,
German, Spanish, French and Italian. Each has a complete catalog for the keys
implemented here. Registry entries do not prove that a user's game installation
contains translations. Game labels and dialogue remain in the resource language;
the app does not invent or bundle translations of that content.

Use stable semantic keys and UTF-8 flat JSON. Add a key to every catalog in the
same change. To add a language, add its ISO language code JSON file and, when the
engine uses a language name, add that name to `GameLanguage.code` and `GameLanguage.name`. Regional
BCP 47 tags resolve to the base language. Unknown languages fall back to English.
Run `python3 tools/android/menu_locales.py`; packaging and release CI also verify
the catalogs. Indexed format arguments may be reordered; preserve their conversion,
flags and precision (for example `%1$,d` and `%2$.1f`). Number punctuation follows
the chosen locale. To check against an installed game's registry without bundling it,
run `python3 tools/android/menu_locales.py --game-language-ini /path/to/game/RESOURCE/INI/texts/language.ini`.
This fails if a registered language lacks a mapped UI catalog. Text and numbers use Android font fallback.

They cover the few texts the Java host still shows itself: the download library's
notifications and states, the look area's accessibility label, the measured
graphics preset toast and the title of the interface failure dialog. Everything else the player reads is in the UI pack
(`mobile-ui/design/strings`).
Translations and layout need native-speaker and real-device review.
