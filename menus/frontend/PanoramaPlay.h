#pragma once
#include "Framework.h"
#include "Action.h"
#include "PanoramaChrome.h"
#include <string>
#include <vector>

class CMenuPanoramaPlay : public CMenuFramework
{
public:
    CMenuPanoramaPlay();
    void Show() override;
    void Draw() override;
    bool KeyDown(int key) override;
    void Dump();
    void SetChrome(void (*draw)(CMenuBaseWindow *)) { m_drawChrome=draw; }
    void SelectMap(const char *name);
private:
    struct Map { std::string name, title, radar; };
    class Card : public CMenuAction
    {
    public:
        Card():index(-1) {}
        void Draw() override;
        int index;
    };
    void _Init() override;
    void _VidInit() override;
    void ReloadMaps();
    void Refresh();
    void Previous();
    void Next();
    void Difficulty();
    void Start();
    void Back();
    void Choose(CMenuBaseItem *item);
    void PaintCard(Card &card);
    void Button(CMenuAction &item,const char *label,const char *tag);
    std::vector<Map> m_maps;
    Card m_cards[8];
    CMenuAction m_previous,m_next,m_difficultyButton,m_start,m_back;
    char m_cardTags[8][32];
    int m_page,m_selected,m_difficulty;
    void (*m_drawChrome)(CMenuBaseWindow *);
    CMenuPanoramaChrome m_chrome;
};
