#pragma once
#include "Framework.h"
#include "Action.h"
#include "Field.h"
#include "PanoramaChrome.h"
#include <string>
#include <vector>

class CMenuPanoramaSettings : public CMenuFramework
{
public:
    CMenuPanoramaSettings();
    void Show() override;
    void Hide() override;
    void Draw() override;
    bool KeyDown(int key) override;
    bool KeyUp(int key) override;
    bool MouseMove(int x,int y) override;
    void Dump();
    void SetChrome(void (*draw)(CMenuBaseWindow *)) { m_drawChrome=draw; }
    void SetBack(void (*back)()) { m_backCallback=back; }
    void SelectCategory(int category);
private:
    enum Kind { Number, Choice, Binding, Pitch, Resolution, PlayerName };
    struct Setting
    {
        int category;Kind kind;const char *label,*key,*defaultValue;
        float minimum,maximum,step;const char *choices,*values;
    };
    struct Original { std::string name,value; };
    class Row : public CMenuAction
    {
    public:
        Row():index(-1),dragging(false) {}
        void Draw() override;
        bool KeyDown(int key) override;
        bool KeyUp(int key) override;
        bool MouseMove(int x,int y) override;
        int index;bool dragging;
    };
    void _Init() override;
    void _VidInit() override;
    void Refresh();
    void Scroll(int rows);
    void ChooseTab(CMenuBaseItem *item);
    void Activate(CMenuBaseItem *item);
    void Adjust(int index,int direction);
    void SliderAt(Row &row,int x);
    void PaintRow(Row &row);
    void Save();
    void NameChanged();
    void Revert();
    void CrosshairDefaults();
    void CrosshairDemo(CMenuBaseItem *item);
    void PaintCrosshair();
    void Back();
    void OpenDisplay();
    void CycleDisplayMode(int direction=1);
    void CycleDisplayWindow(int direction=1);
    void ApplyDisplay();
    void CancelDisplay();
    void RefreshDisplay();
    void PaintDisplay();
    bool Available(const Setting &setting) const;
    std::string Value(const Setting &setting) const;
    void Remember(const char *name);
    static const Setting *Definitions(int &count);
    void Button(CMenuAction &item,const char *label,const char *tag);
    CMenuAction m_tabs[5],m_back,m_save,m_revert,m_up,m_down,m_crosshairDefaults,m_crosshairDemo[3];
    int m_crosshairDemoMode=0;
    CMenuAction m_displayMode,m_displayWindow,m_displayApply,m_displayCancel;
    Row m_rows[9];
    CMenuField m_playerName;
    bool m_nameLoading=false,m_nameInvalid=false;
    char m_rowTags[9][32];
    std::vector<int> m_visible;
    std::vector<Original> m_original;
    std::string m_originalBindings[256];
    bool m_touchedBindings[256];
    int m_category,m_scroll,m_capture,m_captureKey;
    bool m_dirty;
    struct VideoMode { int index,width,height; };
    std::vector<VideoMode> m_modes;
    bool m_displayOpen=false,m_displayTesting=false;
    int m_displaySelectedMode=0,m_displayInitialMode=0,m_displaySelectedWindow=0;
    int m_displayOriginalMode=0,m_displayOriginalWindow=0,m_displayOriginalWidth=0,m_displayOriginalHeight=0;
    int m_displayTestUntil=0;
    void (*m_drawChrome)(CMenuBaseWindow *);
    void (*m_backCallback)();
    CMenuPanoramaChrome m_chrome;
};
