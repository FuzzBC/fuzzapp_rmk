package com.fuzz.colors;

/**
 * Metadata for a single settings CATEGORY SECTION (title + row container).
 *
 * Mirrors SettingInfo: defined once, as data, in on-screen order, in
 * SettingsManager._populateCategories(). Each entry is turned into one
 * inflated item_settings_category section (title TextView + empty row
 * container + separator line) by SettingsManager._buildCategorySections() -
 * there is no more one hand-written <LinearLayout> block per category in
 * activity_main.xml, same as settings rows no longer need one hand-written
 * block each.
 *
 *   id    - one of SettingInfo.CAT_* constants. Used purely as the lookup
 *           key into CAT_ContainerById - never sent over UDP.
 *   title - section header text shown above that category's rows
 *           (e.g. "~ TV ~").
 *
 * Usage (see SettingsManager._populateCategories()):
 *
 *   CAT_Info.add(new CategoryInfo(SettingInfo.CAT_TV, "~ TV ~"));
 *           // ^^ on-screen order of sections = order of these .add() calls.
 */
public class CategoryInfo {

    int id;
    String title;
    String icon;

    /**
     * @param id    One of the SettingInfo.CAT_* constants.
     * @param title Section header text shown above this category's rows.
     */
    public CategoryInfo(int id, String title) {
        this.id = id; this.title = title; this.icon = null;
    }

    /** @param icon Emoji shown left of the title header. */
    public CategoryInfo(int id, String title, String icon) {
        this.id = id; this.title = title; this.icon = icon;
    }

    // Getters
    public int id()     { return id; }
    public String title() { return title; }
    public String icon()  { return icon; }
}
