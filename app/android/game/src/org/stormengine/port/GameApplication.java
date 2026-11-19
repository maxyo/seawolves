package org.stormengine.port;

import android.app.Application;

import java.io.File;

/** Runs in every app process, including the separate download-library process. */
public final class GameApplication extends Application {
    @Override
    public void onCreate() {
        super.onCreate();
        ProcessMarker.init(new File(getNoBackupFilesDir(), "process-markers"));
    }
}
