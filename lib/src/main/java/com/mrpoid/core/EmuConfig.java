package com.mrpoid.core;

/**
 * @author yichou 2018-6-15
 */
public class EmuConfig {
    public static final int SCALE_ORIGINAL = 0, SCALE_STRE = 1, SCALE_2X = 2, SCALE_PRO = 3;

    /**
     * ListPreference / scaling_mode_entryvalues: stretch, proportional, 2x, original
     */
    public static int scaleModeFromPreferenceValue(String value) {
        if (value == null) {
            return SCALE_STRE;
        }
        switch (value) {
            case "stretch":
                return SCALE_STRE;
            case "proportional":
                return SCALE_PRO;
            case "2x":
                return SCALE_2X;
            case "original":
                return SCALE_ORIGINAL;
            default:
                return SCALE_STRE;
        }
    }

    public static String preferenceValueForScaleMode(int mode) {
        switch (mode) {
            case SCALE_STRE:
                return "stretch";
            case SCALE_PRO:
                return "proportional";
            case SCALE_2X:
                return "2x";
            case SCALE_ORIGINAL:
                return "original";
            default:
                return "stretch";
        }
    }

    /** Order matches {@code scaling_mode_entries} / dialog list indices */
    public static int scaleModeFromListIndex(int index) {
        switch (index) {
            case 0:
                return SCALE_STRE;
            case 1:
                return SCALE_PRO;
            case 2:
                return SCALE_2X;
            case 3:
                return SCALE_ORIGINAL;
            default:
                return SCALE_PRO;
        }
    }

    public static int listIndexForScaleMode(int mode) {
        switch (mode) {
            case SCALE_STRE:
                return 0;
            case SCALE_PRO:
                return 1;
            case SCALE_2X:
                return 2;
            case SCALE_ORIGINAL:
                return 3;
            default:
                return 1;
        }
    }

    /**
     * 屏幕宽
     */
    public int scnw;
    /**
     * 屏幕高
     */
    public int scnh;
    /**
     * 堆大小
     */
    public int heapSize;
    public int scaleMode;
    public int padAlpha;
    public boolean anti;
    /**
     * 系统字体
     */
    public boolean sysFont;
    public boolean catchMenuButton;
    public boolean catchVolumeButton;
    public boolean enableKeyVirb;
    /**
     * 系统字体大小
     */
    public int sysFontSize;
    /**
     * 背景颜色
     */
    public int bgColor;

    public EmuConfig() {
        scnw = 240;
        scnh = 320;
        heapSize = 4096;
        scaleMode = SCALE_PRO;
        anti = true;
        sysFont = false;
        bgColor = 0xfff0f0f0;
        padAlpha = 0x80;
        enableKeyVirb = true;
    }

    private static EmuConfig instance;

    public static EmuConfig getInstance() {
        if(instance == null)
            instance = new EmuConfig();
        return instance;
    }
}


