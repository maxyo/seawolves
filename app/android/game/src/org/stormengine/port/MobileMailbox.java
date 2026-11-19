package org.stormengine.port;

import java.util.ArrayDeque;
import java.util.Iterator;

/**
 * The activity's mailbox of engine commands, read by the engine thread once per frame
 * ({@link GameActivity#pollMobileAction}). Pure Java so the host check runs it.
 *
 * Every message kind is classified here, in {@link #kindOf}, and nowhere else:
 *
 * - STATE: the latest value of a key the engine must end up with (audio focus, "graphics
 *   dirty", the UI's HUD declarations). A newer message of the same key replaces the queued one
 *   (it moves to the tail, after the commands queued before it); a purge never removes it.
 *   Dropping one left the engine in a stale state: a purged "audiofocus:gain" kept the game mute
 *   until the next pause, a purged "graphics:reload" lost the settings' «Готово».
 * - COMMAND: an input meant for the screen, mode or revision it was made in (a tap, a camera
 *   step, Back, an interface answer). A purge (the game lost or changed its screen) drops it.
 *
 * A message of no known kind is a programming error and throws.
 */
final class MobileMailbox {
    enum Kind {
        STATE,
        COMMAND
    }

    /** The most commands the engine may have queued; state messages take one slot per key and always fit. */
    static final int COMMAND_CAPACITY = 128;

    private final ArrayDeque<String> queue = new ArrayDeque<>();
    private int commands;
    private boolean loading;
    private long loadingGeneration;

    static Kind kindOf(String message) {
        if (message.startsWith("audiofocus:")
                || message.equals("graphics:reload")
                || message.startsWith("hudowner:")) return Kind.STATE;
        if (message.startsWith("location-load:")
                || message.startsWith("camera:")
                || message.equals("back")) return Kind.COMMAND;
        throw new IllegalArgumentException(
                "Unclassified engine message (MobileMailbox.kindOf): " + message);
    }

    /** The coalescing key of a state message: messages with the same key replace each other. */
    static String stateKey(String message) {
        // One audio focus state (gain, duck, loss); each HUD declaration and graphics:reload stands
        // for itself.
        return message.startsWith("audiofocus:") ? "audiofocus:" : message;
    }

    /** Queues a message. False: a command did not fit (the engine is not reading); the caller logs it. */
    synchronized boolean offer(String message) {
        if (kindOf(message) == Kind.STATE) {
            final String key = stateKey(message);
            for (Iterator<String> it = queue.iterator(); it.hasNext(); ) {
                final String queued = it.next();
                if (kindOf(queued) == Kind.STATE && stateKey(queued).equals(key)) it.remove();
            }
            queue.addLast(message);
            return true;
        }
        // Commands made for the hidden world must never replay on reveal.
        if (loading
                && !message.startsWith(
                        "location-load:" + Long.toUnsignedString(loadingGeneration) + "\t"))
            return true;
        if (commands >= COMMAND_CAPACITY) return false;
        queue.addLast(message);
        ++commands;
        return true;
    }

    synchronized String poll() {
        final String message = queue.pollFirst();
        if (message != null && kindOf(message) == Kind.COMMAND) --commands;
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
        final String wanted = "location-load:" + Long.toUnsignedString(generation) + "\tcancel";
        for (Iterator<String> it = queue.iterator(); it.hasNext(); ) {
            if (it.next().equals(wanted)) {
                it.remove();
                --commands;
                return true;
            }
        }
        return false;
    }

    /** The game lost or changed its screen: every command goes, every state stays. */
    synchronized void purgeCommands() {
        queue.removeIf(message -> kindOf(message) == Kind.COMMAND);
        commands = 0;
    }
}
