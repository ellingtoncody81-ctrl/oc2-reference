#pragma once
#include "Framework.h"
#include "Action.h"
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
    void SelectCategory(int category);
private:
    enum Kind { Number, Choice, Binding, Pitch, Resolution };
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
    void Revert();
    void Back();
    bool Available(const Setting &setting) const;
    std::string Value(const Setting &setting) const;
    void Remember(const char *name);
    static const Setting *Definitions(int &count);
    void Button(CMenuAction &item,const char *label,const char *tag);
    CMenuAction m_tabs[4],m_back,m_save,m_revert,m_up,m_down;
    Row m_rows[9];
    char m_rowTags[9][32];
    std::vector<int> m_visible;
    std::vector<Original> m_original;
    std::string m_originalBindings[256];
    bool m_touchedBindings[256];
    int m_category,m_scroll,m_capture,m_captureKey;
    bool m_dirty;
    void (*m_drawChrome)(CMenuBaseWindow *);
    CMenuPanoramaChrome m_chrome;
};
