package org.stormengine.port;

import java.util.ArrayDeque;

/** Host checks for the restart at the main menu: start only after the process is gone, once, bounded. */
public final class GameRestartTest {
    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static final class FakeHost implements GameRestart.Host {
        int aliveFor, starts, failures;
        long clock;
        final ArrayDeque<Runnable> queue = new ArrayDeque<>();

        public boolean alive(int pid) {
            return aliveFor-- > 0;
        }

        public void start() {
            starts++;
        }

        public void failed(String reason) {
            failures++;
        }

        public void later(Runnable step, long millis) {
            clock += millis;
            queue.add(step);
        }

        public long now() {
            return clock;
        }

        void run() {
            while (!queue.isEmpty()) queue.poll().run();
        }
    }

    public static void main(String[] arguments) {
        FakeHost host = new FakeHost();
        GameRestart restart = new GameRestart(host);
        host.aliveFor = 3;
        require(
                restart.request(42) && restart.pending() && host.starts == 0,
                "No start while the ending process is listed");
        require(
                !restart.request(42) && !restart.request(43),
                "A second request while waiting is not taken (no double start)");
        host.run();
        require(
                host.starts == 1 && host.failures == 0 && !restart.pending(),
                "One start once the process is gone");

        host.aliveFor = 0;
        require(
                restart.request(7) && host.starts == 2 && !restart.pending(),
                "A process already gone starts at once");

        host.aliveFor = Integer.MAX_VALUE;
        require(restart.request(9), "Stuck process request taken");
        host.run();
        require(
                host.starts == 2
                        && host.failures == 1
                        && !restart.pending()
                        && host.clock >= GameRestart.TIMEOUT_MILLIS,
                "A process that does not end fails loudly after the bound, no start");
        require(!restart.request(-1), "No restart without a pid");
        System.out.println(
                "PASS: restart at the main menu (after the process ends, once, bounded)");
    }
}
