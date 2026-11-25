package org.stormengine.port;

import java.util.ArrayDeque;
import java.util.Iterator;
import java.util.Locale;
import java.util.function.Consumer;

/**
 * The activity's mailbox of engine messages, handed to the engine thread once per frame
 * ({@link GameActivity#deliverMobileActions}). Every message is STATE or COMMAND by its {@link Type};
 * android/game/README.md says what each kind survives.
 */
final class MobileMailbox {
    enum Kind {
        STATE,
        COMMAND
    }

    /** What a message tells the engine. */
    enum Type {
        AUDIO_FOCUS(Kind.STATE),
        GRAPHICS_RELOAD(Kind.STATE),
        HUD_OWNER_LOST(Kind.STATE),
        LOCATION_LOAD_CANCEL(Kind.COMMAND),
        CAMERA_ZOOM(Kind.COMMAND),
        CAMERA_LOOK(Kind.COMMAND),
        BACK(Kind.COMMAND);

        final Kind kind;

        Type(Kind kind) {
            this.kind = kind;
        }
    }

    /** The engine's end: one call per message (GameActivity's natives). */
    interface Engine {
        void audioFocus(boolean audible, boolean ducked);

        void graphicsReload();

        void hudOwnerLost();

        void back();

        void locationLoadCancel(long generation);

        void cameraZoom(float scale);

        void lookDown(float x, float y, long time, float width, float height, float density);

        void lookMove(float x, float y, long time);

        void lookUp(long time);

        void lookCancel();
    }

    /** One message. State messages of one type replace each other. */
    static final class Message {
        static final Message
                GRAPHICS_RELOAD =
                        new Message(
                                Type.GRAPHICS_RELOAD, 0, Engine::graphicsReload, "graphics reload"),
                HUD_OWNER_LOST =
                        new Message(Type.HUD_OWNER_LOST, 0, Engine::hudOwnerLost, "HUD owner lost"),
                BACK = new Message(Type.BACK, 0, Engine::back, "back");

        final Type type;
        private final long generation;
        private final Consumer<Engine> delivery;
        private final String text;

        private Message(Type type, long generation, Consumer<Engine> delivery, String text) {
            this.type = type;
            this.generation = generation;
            this.delivery = delivery;
            this.text = text;
        }

        static Message audioFocus(AudioFocusState.Focus focus) {
            final boolean audible = focus != AudioFocusState.Focus.LOSS,
                    ducked = focus == AudioFocusState.Focus.DUCK;
            return new Message(
                    Type.AUDIO_FOCUS,
                    0,
                    engine -> engine.audioFocus(audible, ducked),
                    "audio focus " + focus.name().toLowerCase(Locale.ROOT));
        }

        /** The camera pinch: the factor of the camera's zoom. */
        static Message cameraZoom(float scale) {
            return new Message(
                    Type.CAMERA_ZOOM,
                    0,
                    engine -> engine.cameraZoom(scale),
                    "camera zoom " + scale);
        }

        /** The world map's look finger went down on a surface of width x height px. */
        static Message lookDown(
                float x, float y, long time, float width, float height, float density) {
            return new Message(
                    Type.CAMERA_LOOK,
                    0,
                    engine -> engine.lookDown(x, y, time, width, height, density),
                    "look down " + x + " " + y + " " + time);
        }

        static Message lookMove(float x, float y, long time) {
            return new Message(
                    Type.CAMERA_LOOK,
                    0,
                    engine -> engine.lookMove(x, y, time),
                    "look move " + x + " " + y + " " + time);
        }

        /** The finger lifted: the drag coasts on. */
        static Message lookUp(long time) {
            return new Message(
                    Type.CAMERA_LOOK, 0, engine -> engine.lookUp(time), "look up " + time);
        }

        /** The gesture was taken away: the camera stops where it is. */
        static Message lookCancel() {
            return new Message(Type.CAMERA_LOOK, 0, Engine::lookCancel, "look cancel");
        }

        /** Cancels the location load of `generation` (unsigned). */
        static Message locationLoadCancel(long generation) {
            return new Message(
                    Type.LOCATION_LOAD_CANCEL,
                    generation,
                    engine -> engine.locationLoadCancel(generation),
                    "location load cancel " + Long.toUnsignedString(generation));
        }

        void deliver(Engine engine) {
            delivery.accept(engine);
        }

        @Override
        public String toString() {
            return text;
        }
    }

    /** The most commands the engine may have queued; state messages take one slot per type and always fit. */
    static final int COMMAND_CAPACITY = 128;

    private final ArrayDeque<Message> queue = new ArrayDeque<>();
    private int commands;
    private boolean loading;
    private long loadingGeneration;

    /** Queues a message. False: a command did not fit (the engine is not reading); the caller logs it. */
    synchronized boolean offer(Message message) {
        if (message.type.kind == Kind.STATE) {
            queue.removeIf(queued -> queued.type == message.type);
            queue.addLast(message);
            return true;
        }
        // Commands made for the hidden world must never replay on reveal.
        if (loading
                && !(message.type == Type.LOCATION_LOAD_CANCEL
                        && message.generation == loadingGeneration)) return true;
        if (commands >= COMMAND_CAPACITY) return false;
        queue.addLast(message);
        ++commands;
        return true;
    }

    synchronized Message poll() {
        final Message message = queue.pollFirst();
        if (message != null && message.type.kind == Kind.COMMAND) --commands;
        return message;
    }

    synchronized boolean loadingBoundary(long generation, boolean active) {
        if (loading == active && (!active || loadingGeneration == generation)) return false;
        purgeCommands(); // preserve STATE messages at both entry and exit
        loading = active;
        loadingGeneration = generation;
        return true;
    }

    /** Loading transaction: consume only its Cancel, without running/draining gameplay input. */
    synchronized boolean pollLocationLoadCancel(long generation) {
        for (Iterator<Message> it = queue.iterator(); it.hasNext(); ) {
            final Message message = it.next();
            if (message.type == Type.LOCATION_LOAD_CANCEL && message.generation == generation) {
                it.remove();
                --commands;
                return true;
            }
        }
        return false;
    }

    /** The game lost or changed its screen: every command goes, every state stays. */
    synchronized void purgeCommands() {
        queue.removeIf(message -> message.type.kind == Kind.COMMAND);
        commands = 0;
    }
}
