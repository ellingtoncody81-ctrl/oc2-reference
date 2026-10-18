#pragma once

// GoldSrc rendering layout for the Source scoreboard's separate team rosters.
// Keep geometry independent of engine state so crowded/resized cases can be
// checked without fabricating player statistics or messages.
namespace scoreboard_layout
{
inline float Min(float a,float b) { return a<b?a:b; }
inline float Max(float a,float b) { return a>b?a:b; }
struct Layout
{
	float x=0,y=0,width=0,height=0,scale=1,row=0,avatar=0,font=0;
	float heading=0,teamHeading=0,columns=0,gap=0,footer=0,teamWidth=0;
	int rows[2]={};
	bool sideBySide=false;
	float TeamX(int index) const { return x+(sideBySide?index*(teamWidth+gap):0); }
	float TeamY(int index) const { return y+heading+(!sideBySide&&index?teamHeading+columns+rows[0]*row+gap:0); }
	float TeamHeight(int index) const { return teamHeading+columns+rows[index]*row; }
	float RowY(int index,int ordinal) const { return TeamY(index)+teamHeading+columns+ordinal*row; }
};
inline Layout Make(float width,float height,float scale,int ct,int t)
{
	Layout out; out.scale=Max(.1f,scale);
	ct=ct<0?0:ct>64?64:ct; t=t<0?0:t>64?64:t;
	out.rows[0]=ct<5?5:ct;out.rows[1]=t<5?5:t;
	out.sideBySide=ct+t>20 || ct>10 || t>10;
	out.width=Min((out.sideBySide?1680:1320)*out.scale,width*.92f);
	out.x=(width-out.width)*.5f;
	out.heading=64*out.scale;out.teamHeading=56*out.scale;out.columns=28*out.scale;
	out.gap=16*out.scale;out.footer=42*out.scale;
	out.teamWidth=out.sideBySide?(out.width-out.gap)*.5f:out.width;
	const float fixed=out.heading+out.footer+(out.sideBySide?1:2)*(out.teamHeading+out.columns)+(out.sideBySide?0:out.gap);
	const int units=out.sideBySide?(out.rows[0]>out.rows[1]?out.rows[0]:out.rows[1]):out.rows[0]+out.rows[1];
	const float available=Min(920*out.scale,height*.88f);
	out.row=Min((out.sideBySide?36:48)*out.scale,Max(1,(available-fixed)/units));
	out.avatar=Min((out.sideBySide?30:42)*out.scale,Max(1,out.row-6*out.scale));
	out.font=Min((out.sideBySide?20:24)*out.scale,Max(1,out.row-6*out.scale));
	out.height=fixed+units*out.row;out.y=(height-out.height)*.5f;
	return out;
}
inline bool Before(int scoreA,int deathsA,int slotA,int scoreB,int deathsB,int slotB)
{
	return scoreA!=scoreB?scoreA>scoreB:deathsA!=deathsB?deathsA<deathsB:slotA<slotB;
}
}
