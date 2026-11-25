package org.stormengine.port;

/**
 * «Обработка текстур»: the three switches of what the phone makes from the game's textures in the background, off
 * until the player turns them on.
 */
final class TextureProcessing {
    static final String NORMAL_MAPS = AppNames.SETTING_TEXTURE_NORMAL_MAPS,
            HEIGHT_MAPS = AppNames.SETTING_TEXTURE_HEIGHT_MAPS,
            UPSCALE = AppNames.SETTING_TEXTURE_UPSCALE;

    /** Each switch before the player sets it: off. */
    static final int DEFAULT = 0;

    private TextureProcessing() {}

    static boolean isKey(String key) {
        return key.equals(NORMAL_MAPS) || key.equals(HEIGHT_MAPS) || key.equals(UPSCALE);
    }
}
