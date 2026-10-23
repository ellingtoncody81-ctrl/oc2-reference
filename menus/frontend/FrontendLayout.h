#pragma once

// Native menu coordinates are uniformly scaled from a 768-high canvas.
// Keep layout independent of engine input/drawing so hit regions and paint
// always share one geometry at 720p, 1080p and narrower aspect ratios.
namespace oc2_frontend
{
struct FrontendRect
{
    int x,y,w,h;
    FrontendRect(int px=0,int py=0,int pw=0,int ph=0):x(px),y(py),w(pw),h(ph) {}
};
struct FrontendLayout
{
    int width,columns,pageSize;
    FrontendRect content,profile,navPlay,navInventory,navLoadout,settings,exit;
    FrontendRect local,internet,lan,multiplayer,player,refresh,previous,next;
    FrontendRect filters[4],cards[8];
    explicit FrontendLayout(int canvasWidth):width(canvasWidth)
    {
        const int margin=40,gap=24,profileWidth=canvasWidth<1150?224:264;
        profile=FrontendRect(width-margin-profileWidth,128,profileWidth,548);
        content=FrontendRect(margin,128,profile.x-gap-margin,548);
        navPlay=FrontendRect(200,38,108,48);
        navInventory=FrontendRect(320,38,140,48);
        navLoadout=FrontendRect(472,38,132,48);
        settings=FrontendRect(width-276,38,132,48);
        exit=FrontendRect(width-132,38,92,48);
        local=FrontendRect(content.x+24,content.y+308,content.w-48,80);
        const int half=(content.w-64)/2;
        internet=FrontendRect(content.x+24,content.y+404,half,72);
        lan=FrontendRect(content.x+40+half,content.y+404,half,72);
        multiplayer=FrontendRect(content.x+24,content.y+492,content.w-48,36);
        player=FrontendRect(profile.x+20,profile.y+220,profile.w-40,44);
        refresh=FrontendRect(content.x+content.w-184,content.y+52,160,42);
        previous=FrontendRect(content.x+24,content.y+494,112,38);
        next=FrontendRect(content.x+148,content.y+494,112,38);
        filters[0]=FrontendRect(content.x+24,content.y+132,96,36);
        filters[1]=FrontendRect(content.x+132,content.y+132,136,36);
        filters[2]=FrontendRect(content.x+280,content.y+132,136,36);
        filters[3]=FrontendRect(content.x+428,content.y+132,112,36);
        columns=canvasWidth<1150?3:4;
        pageSize=columns*2;
        const int cardWidth=(content.w-48-(columns-1)*12)/columns;
        for(int i=0;i<8;++i)
            cards[i]=FrontendRect(content.x+24+(i%columns)*(cardWidth+12),
                content.y+194+(i/columns)*132,cardWidth,120);
    }
};
}
