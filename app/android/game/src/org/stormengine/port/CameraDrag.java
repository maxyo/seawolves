package org.stormengine.port;

/**
 * Turns the camera finger's motion into the engine's relative mouse counts without losing any
 * of it. SDL takes a relative mouse move as whole pixels (SDL_androidmouse.c casts the float
 * to int, toward zero), while one touch event often carries less than a pixel: a slow drag,
 * a 120-240 Hz digitizer, and above all every event of a second finger (the stick) moving,
 * in which the camera finger moved by a fraction. Passing each event's delta straight on drops
 * those fractions (the camera would hardly follow, and stop while the stick moves), so
 * the remainder is kept and added to the next event; only whole counts leave.
 */
final class CameraDrag {
    private float restX, restY;

    /** The whole counts to send for this motion (x, y); the fraction waits for the next one. */
    int[] take(float dx, float dy) {
        restX += dx;
        restY += dy;
        final int x = (int) restX, y = (int) restY; // toward zero, as SDL would
        restX -= x;
        restY -= y;
        return new int[] {x, y};
    }

    /** A new drag starts from nothing: a fraction of the last one is not motion of this one. */
    void reset() {
        restX = restY = 0;
    }
}
