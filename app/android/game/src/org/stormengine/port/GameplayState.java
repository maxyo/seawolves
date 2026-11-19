package org.stormengine.port;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

/** Engine-owned mode and bound controls; availability is not readiness or inventory. */
final class GameplayState {
    static final GameplayState UNKNOWN =
            new GameplayState(0, "unknown", "", "", false, 0, Collections.emptySet());
    final long revision, movementGeneration;
    final boolean analogMovement;
    final String mode, language, inputOwner;
    private final Set<String> controls;

    private GameplayState(
            long revision,
            String mode,
            String language,
            String inputOwner,
            boolean analogMovement,
            long movementGeneration,
            Set<String> controls) {
        this.revision = revision;
        this.mode = mode;
        this.language = language;
        this.inputOwner = inputOwner;
        this.analogMovement = analogMovement;
        this.movementGeneration = movementGeneration;
        this.controls = controls;
    }

    static GameplayState read(JSONObject model) {
        if (model.optInt("schemaVersion") != 1 || model.optLong("revision") <= 0) return UNKNOWN;
        String mode = model.optString("mode");
        if (!mode.equals("explore")
                && !mode.equals("melee")
                && !mode.equals("sailing")
                && !mode.equals("naval")
                && !mode.equals("worldmap")) mode = "unknown";
        HashSet<String> controls = new HashSet<>();
        JSONArray list = model.optJSONArray("capabilities");
        if (!mode.equals("unknown") && list != null)
            for (int i = 0; i < Math.min(list.length(), 128); i++) controls.add(list.optString(i));
        Object owner = model.opt("inputOwner");
        String inputOwner =
                (mode.equals("explore") || mode.equals("melee")) && owner instanceof String
                        ? (String) owner
                        : "";
        Object generation = model.opt("movementGeneration");
        long movementGeneration = 0;
        if (generation instanceof Number) {
            double number = ((Number) generation).doubleValue();
            if (Double.isFinite(number)
                    && number > 0
                    && number < 0x1p63
                    && number == Math.floor(number))
                movementGeneration = ((Number) generation).longValue();
        }
        boolean analog =
                Boolean.TRUE.equals(model.opt("analogMovement"))
                        && !inputOwner.isEmpty()
                        && movementGeneration > 0;
        return new GameplayState(
                model.optLong("revision"),
                mode,
                model.optString("language"),
                inputOwner,
                analog,
                analog ? movementGeneration : 0,
                Collections.unmodifiableSet(controls));
    }

    boolean worldmap() {
        return mode.equals("worldmap");
    }

    boolean sea() {
        return mode.equals("sailing") || mode.equals("naval");
    }

    boolean land() {
        return mode.equals("explore") || mode.equals("melee");
    }

    boolean sameLandInputOwner(GameplayState next) {
        return land() && next.land() && !inputOwner.isEmpty() && inputOwner.equals(next.inputOwner);
    }

    boolean same(GameplayState next) {
        return revision == next.revision
                && mode.equals(next.mode)
                && language.equals(next.language)
                && inputOwner.equals(next.inputOwner)
                && analogMovement == next.analogMovement
                && movementGeneration == next.movementGeneration
                && controls.equals(next.controls);
    }
}
