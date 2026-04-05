#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#include <fcitx-config/iniparser.h>
#include <fcitx-utils/standardpaths.h>

namespace fcitx {
namespace webpanel {

constexpr char kConfigPath[] = "conf/webpanel.conf";

enum class Theme { System = 0, Light = 1, Dark = 2 };
enum class Layout { Horizontal = 0, Vertical = 1 };
enum class WritingMode { HorizontalTB = 0, VerticalRL = 1, VerticalLR = 2 };

inline std::string trimCopy(std::string value) {
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(),
                value.end());
    return value;
}

inline std::string lowercaseCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return std::tolower(ch); });
    return value;
}

inline bool iequals(std::string lhs, std::string rhs) {
    return lowercaseCopy(trimCopy(std::move(lhs))) ==
           lowercaseCopy(trimCopy(std::move(rhs)));
}

inline std::string boolString(bool value) { return value ? "True" : "False"; }

inline bool parseBool(std::string value, bool fallback) {
    value = lowercaseCopy(trimCopy(std::move(value)));
    if (value.empty()) {
        return fallback;
    }
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        return false;
    }
    return fallback;
}

inline int parseInt(std::string value, int fallback, int minValue,
                    int maxValue) {
    try {
        const int parsed = std::stoi(trimCopy(std::move(value)));
        if (parsed < minValue) {
            return minValue;
        }
        if (parsed > maxValue) {
            return maxValue;
        }
        return parsed;
    } catch (...) {
        return fallback;
    }
}

inline std::string rawString(const RawConfig &raw, const char *path,
                             const std::string &fallback = {}) {
    if (const auto *value = raw.valueByPath(path)) {
        return std::string(*value);
    }
    return fallback;
}

inline bool rawBool(const RawConfig &raw, const char *path, bool fallback) {
    return parseBool(rawString(raw, path), fallback);
}

inline int rawInt(const RawConfig &raw, const char *path, int fallback,
                  int minValue, int maxValue) {
    return parseInt(rawString(raw, path), fallback, minValue, maxValue);
}

inline Theme parseTheme(const std::string &value, Theme fallback) {
    if (iequals(value, "Light")) {
        return Theme::Light;
    }
    if (iequals(value, "Dark")) {
        return Theme::Dark;
    }
    if (iequals(value, "System")) {
        return Theme::System;
    }
    return fallback;
}

inline Layout parseLayout(const std::string &value, Layout fallback) {
    if (iequals(value, "Vertical")) {
        return Layout::Vertical;
    }
    if (iequals(value, "Horizontal")) {
        return Layout::Horizontal;
    }
    return fallback;
}

inline WritingMode parseWritingMode(const std::string &value,
                                    WritingMode fallback) {
    if (iequals(value, "Vertical right-left") || iequals(value, "VerticalRL")) {
        return WritingMode::VerticalRL;
    }
    if (iequals(value, "Vertical left-right") || iequals(value, "VerticalLR")) {
        return WritingMode::VerticalLR;
    }
    if (iequals(value, "Horizontal top-bottom") ||
        iequals(value, "HorizontalTB")) {
        return WritingMode::HorizontalTB;
    }
    return fallback;
}

inline const char *themeString(Theme value) {
    switch (value) {
    case Theme::Light:
        return "Light";
    case Theme::Dark:
        return "Dark";
    case Theme::System:
    default:
        return "System";
    }
}

inline const char *layoutString(Layout value) {
    switch (value) {
    case Layout::Vertical:
        return "Vertical";
    case Layout::Horizontal:
    default:
        return "Horizontal";
    }
}

inline const char *writingModeString(WritingMode value) {
    switch (value) {
    case WritingMode::VerticalRL:
        return "Vertical right-left";
    case WritingMode::VerticalLR:
        return "Vertical left-right";
    case WritingMode::HorizontalTB:
    default:
        return "Horizontal top-bottom";
    }
}

inline std::string jsonEscape(const std::string &text) {
    std::string escaped;
    escaped.reserve(text.size() + 16);
    for (unsigned char ch : text) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (ch < 0x20) {
                static constexpr char kHex[] = "0123456789abcdef";
                escaped += "\\u00";
                escaped.push_back(kHex[(ch >> 4) & 0x0f]);
                escaped.push_back(kHex[ch & 0x0f]);
            } else {
                escaped.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return escaped;
}

struct ColorModeConfig {
    bool overrideDefault = false;
    bool sameWithLightMode = false;
    std::string highlightColor = "#0000ff";
    std::string highlightHoverColor = "#00007f";
    std::string highlightTextColor = "#ffffff";
    std::string highlightTextPressColor = "#7f7f7f";
    std::string highlightLabelColor = "#ffffff";
    std::string highlightCommentColor = "#ffffff";
    std::string highlightMarkColor = "#ffffff";
    std::string panelColor = "#ffffff";
    std::string textColor = "#000000";
    std::string labelColor = "#000000";
    std::string commentColor = "#000000";
    std::string pagingButtonColor = "#000000";
    std::string disabledPagingButtonColor = "#7f7f7f";
    std::string auxColor = "#000000";
    std::string preeditColorPreCaret = "#000000";
    std::string preeditColorCaret = "#000000";
    std::string preeditColorPostCaret = "#000000";
    std::string borderColor = "#000000";
    std::string dividerColor = "#000000";
};

struct BasicConfig {
    Theme theme = Theme::System;
    std::string defaultTheme = "System";
};

struct TypographyConfig {
    Layout layout = Layout::Horizontal;
    WritingMode writingMode = WritingMode::HorizontalTB;
    bool verticalCommentsAlignRight = false;
    std::string pagingButtonsStyle = "Arrow";
};

struct ScrollModeConfig {
    bool showScrollBar = true;
    bool animation = true;
    int maxRowCount = 6;
    int maxColumnCount = 6;
};

struct BackgroundConfig {
    std::string imageUrl;
    bool keepPanelColorWhenHasImage = false;
    std::string blur = "System";
    bool shadow = true;
};

struct FontConfig {
    std::string textFontFamily;
    int textFontSize = 16;
    int textFontWeight = 400;
    std::string labelFontFamily;
    int labelFontSize = 12;
    int labelFontWeight = 400;
    std::string commentFontFamily;
    int commentFontSize = 12;
    int commentFontWeight = 400;
    std::string preeditFontFamily;
    int preeditFontSize = 16;
    int preeditFontWeight = 400;
};

struct CaretConfig {
    std::string style = "Blink";
    std::string text = "\xE2\x80\xB8";
};

struct HighlightConfig {
    std::string markStyle = "None";
    std::string markText = "\xF0\x9F\x90\xA7";
    std::string hoverBehavior = "None";
};

struct SizeConfig {
    bool overrideDefault = false;
    int borderWidth = 1;
    int borderRadius = 6;
    int margin = 0;
    int highlightRadius = 0;
    int topPadding = 3;
    int rightPadding = 7;
    int bottomPadding = 3;
    int leftPadding = 7;
    int labelTextGap = 6;
    int verticalMinWidth = 200;
    int scrollCellWidth = 65;
    int horizontalDividerWidth = 1;
};

struct AdvancedConfig {
    std::string userCss;
};

struct WebPanelConfig {
    BasicConfig basic;
    ColorModeConfig lightMode;
    ColorModeConfig darkMode = [] {
        ColorModeConfig cfg;
        cfg.panelColor = "#000000";
        cfg.textColor = "#ffffff";
        cfg.labelColor = "#ffffff";
        cfg.commentColor = "#ffffff";
        cfg.pagingButtonColor = "#ffffff";
        cfg.auxColor = "#ffffff";
        cfg.preeditColorPreCaret = "#ffffff";
        cfg.preeditColorCaret = "#ffffff";
        cfg.preeditColorPostCaret = "#ffffff";
        cfg.borderColor = "#ffffff";
        cfg.dividerColor = "#ffffff";
        return cfg;
    }();
    TypographyConfig typography;
    ScrollModeConfig scrollMode;
    BackgroundConfig background;
    FontConfig font;
    CaretConfig caret;
    HighlightConfig highlight;
    SizeConfig size;
    AdvancedConfig advanced;

    void load(const RawConfig &raw) {
        basic.theme = parseTheme(rawString(raw, "Basic/Theme"), basic.theme);
        basic.defaultTheme =
            rawString(raw, "Basic/DefaultTheme", basic.defaultTheme);

        loadColorMode(raw, "LightMode", lightMode, false);
        loadColorMode(raw, "DarkMode", darkMode, true);

        typography.layout =
            parseLayout(rawString(raw, "Typography/Layout"), typography.layout);
        typography.writingMode = parseWritingMode(
            rawString(raw, "Typography/WritingMode"), typography.writingMode);
        typography.verticalCommentsAlignRight =
            rawBool(raw, "Typography/VerticalCommentsAlignRight",
                    typography.verticalCommentsAlignRight);
        typography.pagingButtonsStyle = rawString(
            raw, "Typography/PagingButtonsStyle", typography.pagingButtonsStyle);

        scrollMode.maxRowCount = rawInt(raw, "ScrollMode/MaxRowCount",
                                        scrollMode.maxRowCount, 1, 10);
        scrollMode.maxColumnCount = rawInt(raw, "ScrollMode/MaxColumnCount",
                                           scrollMode.maxColumnCount, 1, 10);
        scrollMode.showScrollBar = rawBool(raw, "ScrollMode/ShowScrollBar",
                                           scrollMode.showScrollBar);
        scrollMode.animation =
            rawBool(raw, "ScrollMode/Animation", scrollMode.animation);

        background.imageUrl =
            rawString(raw, "Background/ImageUrl", background.imageUrl);
        background.keepPanelColorWhenHasImage = rawBool(
            raw, "Background/KeepPanelColorWhenHasImage",
            background.keepPanelColorWhenHasImage);
        background.blur = rawString(raw, "Background/Blur", background.blur);
        background.shadow =
            rawBool(raw, "Background/Shadow", background.shadow);

        font.textFontFamily =
            rawString(raw, "Font/TextFontFamily/0", font.textFontFamily);
        font.textFontSize =
            rawInt(raw, "Font/TextFontSize", font.textFontSize, 4, 100);
        font.textFontWeight =
            rawInt(raw, "Font/TextFontWeight", font.textFontWeight, 1, 1000);
        font.labelFontFamily =
            rawString(raw, "Font/LabelFontFamily/0", font.labelFontFamily);
        font.labelFontSize =
            rawInt(raw, "Font/LabelFontSize", font.labelFontSize, 4, 100);
        font.labelFontWeight =
            rawInt(raw, "Font/LabelFontWeight", font.labelFontWeight, 1, 1000);
        font.commentFontFamily =
            rawString(raw, "Font/CommentFontFamily/0", font.commentFontFamily);
        font.commentFontSize =
            rawInt(raw, "Font/CommentFontSize", font.commentFontSize, 4, 100);
        font.commentFontWeight = rawInt(raw, "Font/CommentFontWeight",
                                        font.commentFontWeight, 1, 1000);
        font.preeditFontFamily =
            rawString(raw, "Font/PreeditFontFamily/0", font.preeditFontFamily);
        font.preeditFontSize =
            rawInt(raw, "Font/PreeditFontSize", font.preeditFontSize, 4, 100);
        font.preeditFontWeight = rawInt(raw, "Font/PreeditFontWeight",
                                        font.preeditFontWeight, 1, 1000);

        caret.style = rawString(raw, "Caret/Style", caret.style);
        caret.text = rawString(raw, "Caret/Text", caret.text);

        highlight.markStyle =
            rawString(raw, "Highlight/MarkStyle", highlight.markStyle);
        highlight.markText =
            rawString(raw, "Highlight/MarkText", highlight.markText);
        highlight.hoverBehavior =
            rawString(raw, "Highlight/HoverBehavior", highlight.hoverBehavior);

        size.overrideDefault =
            rawBool(raw, "Size/OverrideDefault", size.overrideDefault);
        size.borderWidth =
            rawInt(raw, "Size/BorderWidth", size.borderWidth, 0, 10);
        size.borderRadius =
            rawInt(raw, "Size/BorderRadius", size.borderRadius, 0, 100);
        size.margin = rawInt(raw, "Size/Margin", size.margin, 0, 16);
        size.highlightRadius =
            rawInt(raw, "Size/HighlightRadius", size.highlightRadius, 0, 16);
        size.topPadding =
            rawInt(raw, "Size/TopPadding", size.topPadding, 0, 16);
        size.rightPadding =
            rawInt(raw, "Size/RightPadding", size.rightPadding, 0, 16);
        size.bottomPadding =
            rawInt(raw, "Size/BottomPadding", size.bottomPadding, 0, 16);
        size.leftPadding =
            rawInt(raw, "Size/LeftPadding", size.leftPadding, 0, 16);
        size.labelTextGap =
            rawInt(raw, "Size/LabelTextGap", size.labelTextGap, 0, 16);
        size.verticalMinWidth =
            rawInt(raw, "Size/VerticalMinWidth", size.verticalMinWidth, 0, 960);
        size.scrollCellWidth =
            rawInt(raw, "Size/ScrollCellWidth", size.scrollCellWidth, 40, 100);
        size.horizontalDividerWidth = rawInt(raw, "Size/HorizontalDividerWidth",
                                             size.horizontalDividerWidth, 0, 10);

        advanced.userCss =
            rawString(raw, "Advanced/UserCss", advanced.userCss);
    }

    void save(RawConfig &raw) const {
        raw.setValueByPath("Basic/Theme", themeString(basic.theme));
        raw.setValueByPath("Basic/DefaultTheme", basic.defaultTheme);

        saveColorMode(raw, "LightMode", lightMode, false);
        saveColorMode(raw, "DarkMode", darkMode, true);

        raw.setValueByPath("Typography/Layout",
                           layoutString(typography.layout));
        raw.setValueByPath("Typography/WritingMode",
                           writingModeString(typography.writingMode));
        raw.setValueByPath("Typography/VerticalCommentsAlignRight",
                           boolString(typography.verticalCommentsAlignRight));
        raw.setValueByPath("Typography/PagingButtonsStyle",
                           typography.pagingButtonsStyle);

        raw.setValueByPath("ScrollMode/MaxRowCount",
                           std::to_string(scrollMode.maxRowCount));
        raw.setValueByPath("ScrollMode/MaxColumnCount",
                           std::to_string(scrollMode.maxColumnCount));
        raw.setValueByPath("ScrollMode/ShowScrollBar",
                           boolString(scrollMode.showScrollBar));
        raw.setValueByPath("ScrollMode/Animation",
                           boolString(scrollMode.animation));

        raw.setValueByPath("Background/ImageUrl", background.imageUrl);
        raw.setValueByPath("Background/KeepPanelColorWhenHasImage",
                           boolString(background.keepPanelColorWhenHasImage));
        raw.setValueByPath("Background/Blur", background.blur);
        raw.setValueByPath("Background/Shadow", boolString(background.shadow));

        raw.setValueByPath("Font/TextFontFamily/0", font.textFontFamily);
        raw.setValueByPath("Font/TextFontSize",
                           std::to_string(font.textFontSize));
        raw.setValueByPath("Font/TextFontWeight",
                           std::to_string(font.textFontWeight));
        raw.setValueByPath("Font/LabelFontFamily/0", font.labelFontFamily);
        raw.setValueByPath("Font/LabelFontSize",
                           std::to_string(font.labelFontSize));
        raw.setValueByPath("Font/LabelFontWeight",
                           std::to_string(font.labelFontWeight));
        raw.setValueByPath("Font/CommentFontFamily/0", font.commentFontFamily);
        raw.setValueByPath("Font/CommentFontSize",
                           std::to_string(font.commentFontSize));
        raw.setValueByPath("Font/CommentFontWeight",
                           std::to_string(font.commentFontWeight));
        raw.setValueByPath("Font/PreeditFontFamily/0", font.preeditFontFamily);
        raw.setValueByPath("Font/PreeditFontSize",
                           std::to_string(font.preeditFontSize));
        raw.setValueByPath("Font/PreeditFontWeight",
                           std::to_string(font.preeditFontWeight));

        raw.setValueByPath("Caret/Style", caret.style);
        raw.setValueByPath("Caret/Text", caret.text);

        raw.setValueByPath("Highlight/MarkStyle", highlight.markStyle);
        raw.setValueByPath("Highlight/MarkText", highlight.markText);
        raw.setValueByPath("Highlight/HoverBehavior", highlight.hoverBehavior);

        raw.setValueByPath("Size/OverrideDefault",
                           boolString(size.overrideDefault));
        raw.setValueByPath("Size/BorderWidth",
                           std::to_string(size.borderWidth));
        raw.setValueByPath("Size/BorderRadius",
                           std::to_string(size.borderRadius));
        raw.setValueByPath("Size/Margin", std::to_string(size.margin));
        raw.setValueByPath("Size/HighlightRadius",
                           std::to_string(size.highlightRadius));
        raw.setValueByPath("Size/TopPadding", std::to_string(size.topPadding));
        raw.setValueByPath("Size/RightPadding",
                           std::to_string(size.rightPadding));
        raw.setValueByPath("Size/BottomPadding",
                           std::to_string(size.bottomPadding));
        raw.setValueByPath("Size/LeftPadding",
                           std::to_string(size.leftPadding));
        raw.setValueByPath("Size/LabelTextGap",
                           std::to_string(size.labelTextGap));
        raw.setValueByPath("Size/VerticalMinWidth",
                           std::to_string(size.verticalMinWidth));
        raw.setValueByPath("Size/ScrollCellWidth",
                           std::to_string(size.scrollCellWidth));
        raw.setValueByPath("Size/HorizontalDividerWidth",
                           std::to_string(size.horizontalDividerWidth));

        raw.setValueByPath("Advanced/UserCss", advanced.userCss);
    }

    std::string styleJson() const {
        std::ostringstream out;
        out << '{';
        out << "\"Basic\":{\"DefaultTheme\":\""
            << jsonEscape(basic.defaultTheme) << "\"},";
        appendColorModeJson(out, "LightMode", lightMode, false);
        out << ',';
        appendColorModeJson(out, "DarkMode", darkMode, true);
        out << ",\"Typography\":{";
        out << "\"VerticalCommentsAlignRight\":\""
            << boolString(typography.verticalCommentsAlignRight) << "\",";
        out << "\"PagingButtonsStyle\":\""
            << jsonEscape(typography.pagingButtonsStyle) << "\"},";
        out << "\"ScrollMode\":{";
        out << "\"MaxRowCount\":\"" << scrollMode.maxRowCount << "\",";
        out << "\"MaxColumnCount\":\"" << scrollMode.maxColumnCount << "\",";
        out << "\"ShowScrollBar\":\"" << boolString(scrollMode.showScrollBar)
            << "\",";
        out << "\"Animation\":\"" << boolString(scrollMode.animation)
            << "\"},";
        out << "\"Background\":{";
        out << "\"ImageUrl\":\"" << jsonEscape(background.imageUrl) << "\",";
        out << "\"KeepPanelColorWhenHasImage\":\""
            << boolString(background.keepPanelColorWhenHasImage) << "\",";
        out << "\"Blur\":\"" << boolString(blurEnabled()) << "\",";
        out << "\"Shadow\":\"" << boolString(background.shadow) << "\"},";
        out << "\"Font\":{";
        appendFontJson(out, "Text", font.textFontFamily, font.textFontSize,
                       font.textFontWeight);
        out << ',';
        appendFontJson(out, "Label", font.labelFontFamily, font.labelFontSize,
                       font.labelFontWeight);
        out << ',';
        appendFontJson(out, "Comment", font.commentFontFamily,
                       font.commentFontSize, font.commentFontWeight);
        out << ',';
        appendFontJson(out, "Preedit", font.preeditFontFamily,
                       font.preeditFontSize, font.preeditFontWeight);
        out << "},";
        out << "\"Caret\":{\"Style\":\"" << jsonEscape(caret.style)
            << "\",\"Text\":\"" << jsonEscape(caret.text) << "\"},";
        out << "\"Highlight\":{\"MarkStyle\":\""
            << jsonEscape(highlight.markStyle) << "\",\"MarkText\":\""
            << jsonEscape(highlight.markText) << "\",\"HoverBehavior\":\""
            << jsonEscape(highlight.hoverBehavior) << "\"},";
        out << "\"Size\":{";
        out << "\"OverrideDefault\":\"" << boolString(size.overrideDefault)
            << "\",";
        out << "\"BorderWidth\":\"" << size.borderWidth << "\",";
        out << "\"BorderRadius\":\"" << size.borderRadius << "\",";
        out << "\"Margin\":\"" << size.margin << "\",";
        out << "\"HighlightRadius\":\"" << size.highlightRadius << "\",";
        out << "\"TopPadding\":\"" << size.topPadding << "\",";
        out << "\"RightPadding\":\"" << size.rightPadding << "\",";
        out << "\"BottomPadding\":\"" << size.bottomPadding << "\",";
        out << "\"LeftPadding\":\"" << size.leftPadding << "\",";
        out << "\"LabelTextGap\":\"" << size.labelTextGap << "\",";
        out << "\"VerticalMinWidth\":\"" << size.verticalMinWidth << "\",";
        out << "\"ScrollCellWidth\":\"" << size.scrollCellWidth << "\",";
        out << "\"HorizontalDividerWidth\":\""
            << size.horizontalDividerWidth << "\"},";
        out << "\"Advanced\":{\"UserCss\":\"" << jsonEscape(advanced.userCss)
            << "\"}";
        out << '}';
        return out.str();
    }

    bool blurEnabled() const {
        return !iequals(background.blur, "False") &&
               !iequals(background.blur, "None") && !background.blur.empty();
    }

  private:
    static void loadColorMode(const RawConfig &raw, const char *prefix,
                              ColorModeConfig &cfg, bool hasSameWithLightMode) {
        const std::string base(prefix);
        cfg.overrideDefault =
            rawBool(raw, (base + "/OverrideDefault").c_str(),
                    cfg.overrideDefault);
        if (hasSameWithLightMode) {
            cfg.sameWithLightMode =
                rawBool(raw, (base + "/SameWithLightMode").c_str(),
                        cfg.sameWithLightMode);
        }
        cfg.highlightColor =
            rawString(raw, (base + "/HighlightColor").c_str(),
                      cfg.highlightColor);
        cfg.highlightHoverColor =
            rawString(raw, (base + "/HighlightHoverColor").c_str(),
                      cfg.highlightHoverColor);
        cfg.highlightTextColor =
            rawString(raw, (base + "/HighlightTextColor").c_str(),
                      cfg.highlightTextColor);
        cfg.highlightTextPressColor =
            rawString(raw, (base + "/HighlightTextPressColor").c_str(),
                      cfg.highlightTextPressColor);
        cfg.highlightLabelColor =
            rawString(raw, (base + "/HighlightLabelColor").c_str(),
                      cfg.highlightLabelColor);
        cfg.highlightCommentColor =
            rawString(raw, (base + "/HighlightCommentColor").c_str(),
                      cfg.highlightCommentColor);
        cfg.highlightMarkColor =
            rawString(raw, (base + "/HighlightMarkColor").c_str(),
                      cfg.highlightMarkColor);
        cfg.panelColor =
            rawString(raw, (base + "/PanelColor").c_str(), cfg.panelColor);
        cfg.textColor =
            rawString(raw, (base + "/TextColor").c_str(), cfg.textColor);
        cfg.labelColor =
            rawString(raw, (base + "/LabelColor").c_str(), cfg.labelColor);
        cfg.commentColor =
            rawString(raw, (base + "/CommentColor").c_str(), cfg.commentColor);
        cfg.pagingButtonColor = rawString(raw,
                                          (base + "/PagingButtonColor").c_str(),
                                          cfg.pagingButtonColor);
        cfg.disabledPagingButtonColor = rawString(
            raw, (base + "/DisabledPagingButtonColor").c_str(),
            cfg.disabledPagingButtonColor);
        cfg.auxColor =
            rawString(raw, (base + "/AuxColor").c_str(), cfg.auxColor);
        cfg.preeditColorPreCaret =
            rawString(raw, (base + "/PreeditColorPreCaret").c_str(),
                      cfg.preeditColorPreCaret);
        cfg.preeditColorCaret =
            rawString(raw, (base + "/PreeditColorCaret").c_str(),
                      cfg.preeditColorCaret);
        cfg.preeditColorPostCaret =
            rawString(raw, (base + "/PreeditColorPostCaret").c_str(),
                      cfg.preeditColorPostCaret);
        cfg.borderColor =
            rawString(raw, (base + "/BorderColor").c_str(), cfg.borderColor);
        cfg.dividerColor =
            rawString(raw, (base + "/DividerColor").c_str(), cfg.dividerColor);
    }

    static void saveColorMode(RawConfig &raw, const char *prefix,
                              const ColorModeConfig &cfg,
                              bool hasSameWithLightMode) {
        const std::string base(prefix);
        raw.setValueByPath((base + "/OverrideDefault").c_str(),
                           boolString(cfg.overrideDefault));
        if (hasSameWithLightMode) {
            raw.setValueByPath((base + "/SameWithLightMode").c_str(),
                               boolString(cfg.sameWithLightMode));
        }
        raw.setValueByPath((base + "/HighlightColor").c_str(),
                           cfg.highlightColor);
        raw.setValueByPath((base + "/HighlightHoverColor").c_str(),
                           cfg.highlightHoverColor);
        raw.setValueByPath((base + "/HighlightTextColor").c_str(),
                           cfg.highlightTextColor);
        raw.setValueByPath((base + "/HighlightTextPressColor").c_str(),
                           cfg.highlightTextPressColor);
        raw.setValueByPath((base + "/HighlightLabelColor").c_str(),
                           cfg.highlightLabelColor);
        raw.setValueByPath((base + "/HighlightCommentColor").c_str(),
                           cfg.highlightCommentColor);
        raw.setValueByPath((base + "/HighlightMarkColor").c_str(),
                           cfg.highlightMarkColor);
        raw.setValueByPath((base + "/PanelColor").c_str(), cfg.panelColor);
        raw.setValueByPath((base + "/TextColor").c_str(), cfg.textColor);
        raw.setValueByPath((base + "/LabelColor").c_str(), cfg.labelColor);
        raw.setValueByPath((base + "/CommentColor").c_str(), cfg.commentColor);
        raw.setValueByPath((base + "/PagingButtonColor").c_str(),
                           cfg.pagingButtonColor);
        raw.setValueByPath((base + "/DisabledPagingButtonColor").c_str(),
                           cfg.disabledPagingButtonColor);
        raw.setValueByPath((base + "/AuxColor").c_str(), cfg.auxColor);
        raw.setValueByPath((base + "/PreeditColorPreCaret").c_str(),
                           cfg.preeditColorPreCaret);
        raw.setValueByPath((base + "/PreeditColorCaret").c_str(),
                           cfg.preeditColorCaret);
        raw.setValueByPath((base + "/PreeditColorPostCaret").c_str(),
                           cfg.preeditColorPostCaret);
        raw.setValueByPath((base + "/BorderColor").c_str(), cfg.borderColor);
        raw.setValueByPath((base + "/DividerColor").c_str(), cfg.dividerColor);
    }

    static void appendColorModeJson(std::ostringstream &out, const char *name,
                                    const ColorModeConfig &cfg,
                                    bool hasSameWithLightMode) {
        out << '"' << name << "\":{";
        out << "\"OverrideDefault\":\"" << boolString(cfg.overrideDefault)
            << "\",";
        if (hasSameWithLightMode) {
            out << "\"SameWithLightMode\":\""
                << boolString(cfg.sameWithLightMode) << "\",";
        }
        out << "\"HighlightColor\":\"" << jsonEscape(cfg.highlightColor)
            << "\",";
        out << "\"HighlightHoverColor\":\""
            << jsonEscape(cfg.highlightHoverColor) << "\",";
        out << "\"HighlightTextColor\":\""
            << jsonEscape(cfg.highlightTextColor) << "\",";
        out << "\"HighlightTextPressColor\":\""
            << jsonEscape(cfg.highlightTextPressColor) << "\",";
        out << "\"HighlightLabelColor\":\""
            << jsonEscape(cfg.highlightLabelColor) << "\",";
        out << "\"HighlightCommentColor\":\""
            << jsonEscape(cfg.highlightCommentColor) << "\",";
        out << "\"HighlightMarkColor\":\""
            << jsonEscape(cfg.highlightMarkColor) << "\",";
        out << "\"PanelColor\":\"" << jsonEscape(cfg.panelColor) << "\",";
        out << "\"TextColor\":\"" << jsonEscape(cfg.textColor) << "\",";
        out << "\"LabelColor\":\"" << jsonEscape(cfg.labelColor) << "\",";
        out << "\"CommentColor\":\"" << jsonEscape(cfg.commentColor) << "\",";
        out << "\"PagingButtonColor\":\""
            << jsonEscape(cfg.pagingButtonColor) << "\",";
        out << "\"DisabledPagingButtonColor\":\""
            << jsonEscape(cfg.disabledPagingButtonColor) << "\",";
        out << "\"AuxColor\":\"" << jsonEscape(cfg.auxColor) << "\",";
        out << "\"PreeditColorPreCaret\":\""
            << jsonEscape(cfg.preeditColorPreCaret) << "\",";
        out << "\"PreeditColorCaret\":\""
            << jsonEscape(cfg.preeditColorCaret) << "\",";
        out << "\"PreeditColorPostCaret\":\""
            << jsonEscape(cfg.preeditColorPostCaret) << "\",";
        out << "\"BorderColor\":\"" << jsonEscape(cfg.borderColor) << "\",";
        out << "\"DividerColor\":\"" << jsonEscape(cfg.dividerColor) << "\"}";
    }

    static void appendFontJson(std::ostringstream &out, const char *prefix,
                               const std::string &family, int size,
                               int weight) {
        out << '"' << prefix << "FontFamily\":{\"0\":\"" << jsonEscape(family)
            << "\"},";
        out << '"' << prefix << "FontSize\":\"" << size << "\",";
        out << '"' << prefix << "FontWeight\":\"" << weight << '"';
    }
};

inline std::filesystem::path configPathForUser() {
    auto path =
        StandardPaths::global().locate(StandardPathsType::PkgConfig, kConfigPath);
    if (!path.empty()) {
        return path;
    }
    return StandardPaths::global().userDirectory(StandardPathsType::PkgConfig) /
           std::filesystem::path(kConfigPath);
}

inline std::string defaultConfigTextUtf8() {
    return "[Basic]\r\n"
           "Theme=System\r\n"
           "DefaultTheme=System\r\n"
           "\r\n"
           "[LightMode]\r\n"
           "OverrideDefault=False\r\n"
           "HighlightColor=#0000ff\r\n"
           "HighlightHoverColor=#00007f\r\n"
           "HighlightTextColor=#ffffff\r\n"
           "HighlightTextPressColor=#7f7f7f\r\n"
           "HighlightLabelColor=#ffffff\r\n"
           "HighlightCommentColor=#ffffff\r\n"
           "HighlightMarkColor=#ffffff\r\n"
           "PanelColor=#ffffff\r\n"
           "TextColor=#000000\r\n"
           "LabelColor=#000000\r\n"
           "CommentColor=#000000\r\n"
           "PagingButtonColor=#000000\r\n"
           "DisabledPagingButtonColor=#7f7f7f\r\n"
           "AuxColor=#000000\r\n"
           "PreeditColorPreCaret=#000000\r\n"
           "PreeditColorCaret=#000000\r\n"
           "PreeditColorPostCaret=#000000\r\n"
           "BorderColor=#000000\r\n"
           "DividerColor=#000000\r\n"
           "\r\n"
           "[DarkMode]\r\n"
           "OverrideDefault=False\r\n"
           "SameWithLightMode=False\r\n"
           "HighlightColor=#0000ff\r\n"
           "HighlightHoverColor=#00007f\r\n"
           "HighlightTextColor=#ffffff\r\n"
           "HighlightTextPressColor=#7f7f7f\r\n"
           "HighlightLabelColor=#ffffff\r\n"
           "HighlightCommentColor=#ffffff\r\n"
           "HighlightMarkColor=#ffffff\r\n"
           "PanelColor=#000000\r\n"
           "TextColor=#ffffff\r\n"
           "LabelColor=#ffffff\r\n"
           "CommentColor=#ffffff\r\n"
           "PagingButtonColor=#ffffff\r\n"
           "DisabledPagingButtonColor=#7f7f7f\r\n"
           "AuxColor=#ffffff\r\n"
           "PreeditColorPreCaret=#ffffff\r\n"
           "PreeditColorCaret=#ffffff\r\n"
           "PreeditColorPostCaret=#ffffff\r\n"
           "BorderColor=#ffffff\r\n"
           "DividerColor=#ffffff\r\n"
           "\r\n"
           "[Typography]\r\n"
           "Layout=Horizontal\r\n"
           "WritingMode=Horizontal top-bottom\r\n"
           "VerticalCommentsAlignRight=False\r\n"
           "PagingButtonsStyle=Arrow\r\n"
           "\r\n"
           "[ScrollMode]\r\n"
           "MaxRowCount=6\r\n"
           "MaxColumnCount=6\r\n"
           "ShowScrollBar=True\r\n"
           "Animation=True\r\n"
           "\r\n"
           "[Background]\r\n"
           "ImageUrl=\r\n"
           "KeepPanelColorWhenHasImage=False\r\n"
           "Blur=System\r\n"
           "Shadow=True\r\n"
           "\r\n"
           "[Font]\r\n"
           "TextFontSize=16\r\n"
           "TextFontWeight=400\r\n"
           "LabelFontSize=12\r\n"
           "LabelFontWeight=400\r\n"
           "CommentFontSize=12\r\n"
           "CommentFontWeight=400\r\n"
           "PreeditFontSize=16\r\n"
           "PreeditFontWeight=400\r\n"
           "\r\n"
           "[Font/TextFontFamily]\r\n"
           "0=\r\n"
           "\r\n"
           "[Font/LabelFontFamily]\r\n"
           "0=\r\n"
           "\r\n"
           "[Font/CommentFontFamily]\r\n"
           "0=\r\n"
           "\r\n"
           "[Font/PreeditFontFamily]\r\n"
           "0=\r\n"
           "\r\n"
           "[Caret]\r\n"
           "Style=Blink\r\n"
           "Text=\xE2\x80\xB8\r\n"
           "\r\n"
           "[Highlight]\r\n"
           "MarkStyle=None\r\n"
           "MarkText=\xF0\x9F\x90\xA7\r\n"
           "HoverBehavior=None\r\n"
           "\r\n"
           "[Size]\r\n"
           "OverrideDefault=False\r\n"
           "BorderWidth=1\r\n"
           "BorderRadius=6\r\n"
           "Margin=0\r\n"
           "HighlightRadius=0\r\n"
           "TopPadding=3\r\n"
           "RightPadding=7\r\n"
           "BottomPadding=3\r\n"
           "LeftPadding=7\r\n"
           "LabelTextGap=6\r\n"
           "VerticalMinWidth=200\r\n"
           "ScrollCellWidth=65\r\n"
           "HorizontalDividerWidth=1\r\n"
           "\r\n"
           "[Advanced]\r\n"
           "UserCss=\r\n";
}

inline std::string readConfigTextUtf8() {
    const auto path = configPathForUser();
    if (!std::filesystem::exists(path)) {
        return defaultConfigTextUtf8();
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return defaultConfigTextUtf8();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const auto text = ss.str();
    return text.empty() ? defaultConfigTextUtf8() : text;
}

inline bool writeConfigTextUtf8(const std::string &text) {
    const auto path = configPathForUser();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

inline WebPanelConfig loadConfigFromDisk() {
    WebPanelConfig config;
    RawConfig raw;
    try {
        readAsIni(raw, StandardPathsType::PkgConfig, std::filesystem::path(kConfigPath));
        config.load(raw);
    } catch (...) {
    }
    return config;
}

} // namespace webpanel
} // namespace fcitx
