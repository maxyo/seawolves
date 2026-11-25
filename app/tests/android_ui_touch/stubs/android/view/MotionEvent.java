package android.view;

/** The part of MotionEvent the touch splitter and the camera gesture read and build; pointers are {id, x, y}. */
public final class MotionEvent {
    public static final int ACTION_DOWN = 0,
            ACTION_UP = 1,
            ACTION_MOVE = 2,
            ACTION_CANCEL = 3,
            ACTION_POINTER_DOWN = 5,
            ACTION_POINTER_UP = 6;
    public static final int ACTION_MASK = 0xff, ACTION_POINTER_INDEX_SHIFT = 8;

    public static final class PointerProperties {
        public int id;
    }

    public static final class PointerCoords {
        public float x, y;
    }

    private final int action;
    private final long downTime, eventTime;
    private final int[] ids;
    private final float[] xs, ys;
    // Batched samples before this one: times, and per sample the x / y of every pointer.
    private long[] historyTimes = new long[0];
    private float[][] historyXs = new float[0][], historyYs = new float[0][];
    public boolean recycled;

    public MotionEvent(
            long downTime, long eventTime, int action, int[] ids, float[] xs, float[] ys) {
        this.downTime = downTime;
        this.eventTime = eventTime;
        this.action = action;
        this.ids = ids;
        this.xs = xs;
        this.ys = ys;
    }

    public static MotionEvent obtain(
            long downTime,
            long eventTime,
            int action,
            int count,
            PointerProperties[] properties,
            PointerCoords[] coords,
            int metaState,
            int buttonState,
            float xPrecision,
            float yPrecision,
            int deviceId,
            int edgeFlags,
            int source,
            int flags) {
        int[] ids = new int[count];
        float[] xs = new float[count], ys = new float[count];
        for (int i = 0; i < count; i++) {
            ids[i] = properties[i].id;
            xs[i] = coords[i].x;
            ys[i] = coords[i].y;
        }
        return new MotionEvent(downTime, eventTime, action, ids, xs, ys);
    }

    public MotionEvent withHistory(long[] times, float[][] x, float[][] y) {
        historyTimes = times;
        historyXs = x;
        historyYs = y;
        return this;
    }

    public int getHistorySize() {
        return historyTimes.length;
    }

    public long getHistoricalEventTime(int position) {
        return historyTimes[position];
    }

    public float getHistoricalX(int index, int position) {
        return historyXs[position][index];
    }

    public float getHistoricalY(int index, int position) {
        return historyYs[position][index];
    }

    public int getActionMasked() {
        return action & ACTION_MASK;
    }

    public int getActionIndex() {
        return action >> ACTION_POINTER_INDEX_SHIFT;
    }

    public int getPointerCount() {
        return ids.length;
    }

    public int getPointerId(int index) {
        return ids[index];
    }

    public int findPointerIndex(int id) {
        for (int i = 0; i < ids.length; i++) if (ids[i] == id) return i;
        return -1;
    }

    public float getX(int index) {
        return xs[index];
    }

    public float getY(int index) {
        return ys[index];
    }

    public long getDownTime() {
        return downTime;
    }

    public long getEventTime() {
        return eventTime;
    }

    public void getPointerProperties(int index, PointerProperties out) {
        out.id = ids[index];
    }

    public void getPointerCoords(int index, PointerCoords out) {
        out.x = xs[index];
        out.y = ys[index];
    }

    public int getMetaState() {
        return 0;
    }

    public int getButtonState() {
        return 0;
    }

    public float getXPrecision() {
        return 1;
    }

    public float getYPrecision() {
        return 1;
    }

    public int getDeviceId() {
        return 0;
    }

    public int getEdgeFlags() {
        return 0;
    }

    public int getSource() {
        return 0;
    }

    public int getFlags() {
        return 0;
    }

    public void recycle() {
        recycled = true;
    }
}
