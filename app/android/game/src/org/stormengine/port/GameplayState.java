package org.stormengine.port;

import java.util.Locale;

/** Engine-owned mode and the hero who owns land input; availability is not readiness or inventory. */
final class GameplayState {
    /** The engine's gameplay modes (GameplaySnapshot.mode). */
    enum Mode {
        UNKNOWN,
        EXPLORE,
        MELEE,
        SAILING,
        NAVAL,
        WORLDMAP
    }

    static final GameplayState UNKNOWN = new GameplayState(0, Mode.UNKNOWN, "", "", false, 0);
    final long revision, movementGeneration;
    final boolean analogMovement;
    final Mode mode;
    final String language, inputOwner;

    private GameplayState(
            long revision,
            Mode mode,
            String language,
            String inputOwner,
            boolean analogMovement,
            long movementGeneration) {
        this.revision = revision;
        this.mode = mode;
        this.language = language;
        this.inputOwner = inputOwner;
        this.analogMovement = analogMovement;
        this.movementGeneration = movementGeneration;
    }

    /** The engine's snapshot (GameActivity.showGameplayState); a mode it does not name throws. */
    static GameplayState of(
            long revision,
            String mode,
            String language,
            String inputOwner,
            boolean analogMovement,
            long movementGeneration) {
        GameplayState state =
                new GameplayState(
                        revision,
                        Mode.valueOf(mode.toUpperCase(Locale.ROOT)),
                        language,
                        "",
                        false,
                        0);
        if (!state.land()) return state;
        boolean analog = analogMovement && !inputOwner.isEmpty() && movementGeneration != 0;
        return new GameplayState(
                revision,
                state.mode,
                language,
                inputOwner,
                analog,
                analog ? movementGeneration : 0);
    }

    boolean worldmap() {
        return mode == Mode.WORLDMAP;
    }

    boolean sea() {
        return mode == Mode.SAILING || mode == Mode.NAVAL;
    }

    boolean land() {
        return mode == Mode.EXPLORE || mode == Mode.MELEE;
    }

    boolean sameLandInputOwner(GameplayState next) {
        return land() && next.land() && !inputOwner.isEmpty() && inputOwner.equals(next.inputOwner);
    }

    boolean same(GameplayState next) {
        return revision == next.revision
                && mode == next.mode
                && language.equals(next.language)
                && inputOwner.equals(next.inputOwner)
                && analogMovement == next.analogMovement
                && movementGeneration == next.movementGeneration;
    }
}
