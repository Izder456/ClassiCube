#include "LScreens.h"
#include "LWidgets.h"
#include "LWeb.h"
#include "Launcher.h"
#include "Gui.h"
#include "Game.h"
#include "Drawer2D.h"
#include "ExtMath.h"
#include "Platform.h"
#include "Stream.h"
#include "Funcs.h"
#include "Resources.h"

/*########################################################################################################################*
*---------------------------------------------------------Screen base-----------------------------------------------------*
*#########################################################################################################################*/
static void LScreen_NullFunc(struct LScreen* s) { }
CC_NOINLINE static int LScreen_IndexOf(struct LScreen* s, void* w) {
	int i;
	for (i = 0; i < s->NumWidgets; i++) {
		if (s->Widgets[i] == w) return i;
	}
	return -1;
}

CC_NOINLINE static struct LWidget* LScreen_WidgetAt(struct LScreen* s, int x, int y) {
	struct LWidget* w;
	int i = 0;

	for (i = 0; i < s->NumWidgets; i++) {
		w = s->Widgets[i];
		if (w->Hidden) continue;

		if (Gui_Contains(w->X, w->Y, w->Width, w->Height, x, y)) return w;
	}
	return NULL;
}

static void LScreen_Draw(struct LScreen* s) {
	int i;
	for (i = 0; i < s->NumWidgets; i++) {
		LWidget_Draw(s->Widgets[i]);
	}
}

static void LScreen_Tick(struct LScreen* s) {
	struct LWidget* w = s->SelectedWidget;
	if (w && w->VTABLE->Tick) w->VTABLE->Tick(w);
}

CC_NOINLINE static void LScreen_HoverWidget(struct LScreen* s, struct LWidget* w) {
	if (!w) return;
	w->Hovered       = true;
	s->HoveredWidget = w;
	if (w->VTABLE->OnHover) w->VTABLE->OnHover(w);
}

CC_NOINLINE static void LScreen_UnhoverWidget(struct LScreen* s, struct LWidget* w) {
	if (!w) return;
	w->Hovered       = false;
	s->HoveredWidget = NULL;
	if (w->VTABLE->OnUnhover) w->VTABLE->OnUnhover(w);
}

CC_NOINLINE static void LScreen_SelectWidget(struct LScreen* s, struct LWidget* w, bool was) {
	if (!w) return;
	w->Selected      = true;
	s->SelectedWidget = w;
	if (w->VTABLE->OnSelect) w->VTABLE->OnSelect(w, was);
}

CC_NOINLINE static void LScreen_UnselectWidget(struct LScreen* s, struct LWidget* w) {
	if (!w) return;
	w->Selected       = false;
	s->SelectedWidget = NULL;
	if (w->VTABLE->OnUnselect) w->VTABLE->OnUnselect(w);
}

static void LScreen_HandleTab(struct LScreen* s) {
	struct LWidget* w;
	int dir   = Key_IsShiftPressed() ? -1 : 1;
	int index = 0, i, j;

	if (s->SelectedWidget) {
		index = LScreen_IndexOf(s, s->SelectedWidget) + dir;
	}

	for (j = 0; j < s->NumWidgets; j++) {
		i = (index + j * dir) % s->NumWidgets;
		if (i < 0) i += s->NumWidgets;

		w = s->Widgets[i];
		if (w->Hidden || !w->TabSelectable) continue;

		LScreen_UnselectWidget(s, s->SelectedWidget);
		LScreen_SelectWidget(s, w, false);
		return;
	}
}

static void LScreen_KeyDown(struct LScreen* s, Key key) {
	if (key == KEY_TAB) {
		LScreen_HandleTab(s);
	} else if (key == KEY_ENTER) {
		if (s->SelectedWidget && s->SelectedWidget->OnClick) {
			s->SelectedWidget->OnClick(s->SelectedWidget, Mouse_X, Mouse_Y);
		} else if (s->OnEnterWidget) {
			s->OnEnterWidget->OnClick(s->OnEnterWidget,   Mouse_X, Mouse_Y);
		}
	} else if (s->SelectedWidget) {
		s->SelectedWidget->VTABLE->OnKeyDown(s->SelectedWidget, key);
	}
}

static void LScreen_KeyPress(struct LScreen* s, char key) {
	if (!s->SelectedWidget) return;
	s->SelectedWidget->VTABLE->OnKeyPress(s->SelectedWidget, key);
}

static void LScreen_MouseDown(struct LScreen* s, MouseButton btn) {
	struct LWidget* over = LScreen_WidgetAt(s, Mouse_X, Mouse_Y);
	struct LWidget* prev = s->SelectedWidget;

	if (prev && over != prev) LScreen_UnselectWidget(s, prev);
	if (over) LScreen_SelectWidget(s, over, over == prev);
}

static void LScreen_MouseUp(struct LScreen* s, MouseButton btn) {
	struct LWidget* over = LScreen_WidgetAt(s, Mouse_X, Mouse_Y);
	struct LWidget* prev = s->SelectedWidget;

	/* if user moves mouse away, it doesn't count */
	if (over != prev) {
		LScreen_UnselectWidget(s, prev);
	} else if (over && over->OnClick) {
		over->OnClick(over, Mouse_X, Mouse_Y);
	}
}

static void LScreen_MouseMove(struct LScreen* s, int deltaX, int deltaY) {
	struct LWidget* over = LScreen_WidgetAt(s, Mouse_X, Mouse_Y);
	struct LWidget* prev = s->HoveredWidget;

	if (over == prev) return;
	if (prev) s->UnhoverWidget(s, prev);
	if (over) s->HoverWidget(s,   over);
}
static void LScreen_MouseWheel(struct LScreen* s, float delta) { }

CC_NOINLINE static void LScreen_Reset(struct LScreen* s) {
	int i;

	s->Init = NULL; /* screens should always override this */
	s->Free = LScreen_NullFunc;
	s->Draw       = LScreen_Draw;
	s->Tick       = LScreen_Tick;
	s->OnDisplay  = LScreen_NullFunc;
	s->KeyDown    = LScreen_KeyDown;
	s->KeyPress   = LScreen_KeyPress;
	s->MouseDown  = LScreen_MouseDown;
	s->MouseUp    = LScreen_MouseUp;
	s->MouseMove  = LScreen_MouseMove;
	s->MouseWheel = LScreen_MouseWheel;
	s->HoverWidget    = LScreen_HoverWidget;
	s->UnhoverWidget  = LScreen_UnhoverWidget;

	/* reset all widgets mouse state */
	for (i = 0; i < s->NumWidgets; i++) { 
		s->Widgets[i]->Hovered  = false;
		s->Widgets[i]->Selected = false;
	}

	s->OnEnterWidget  = NULL;
	s->HoveredWidget  = NULL;
	s->SelectedWidget = NULL;
}


/*########################################################################################################################*
*--------------------------------------------------------Widget helpers---------------------------------------------------*
*#########################################################################################################################*/
CC_NOINLINE static void LScreen_Button(struct LScreen* s, struct LButton* w, int width, int height, const char* text) {
	String str = String_FromReadonly(text);
	LButton_Init(w, &Launcher_TitleFont, width, height);
	LButton_SetText(w, &str);
	s->Widgets[s->NumWidgets++] = (struct LWidget*)w;
}

CC_NOINLINE static void LScreen_Label(struct LScreen* s, struct LLabel* w, const char* text) {
	String str = String_FromReadonly(text);
	LLabel_Init(w, &Launcher_TextFont);
	LLabel_SetText(w, &str);
	s->Widgets[s->NumWidgets++] = (struct LWidget*)w;
}

CC_NOINLINE static void LScreen_Input(struct LScreen* s, struct LInput* w, int width, bool password, const char* hintText) {
	LInput_Init(w, &Launcher_TextFont, width, 30, 
				hintText, &Launcher_HintFont);
	w->Password = password;
	s->Widgets[s->NumWidgets++] = (struct LWidget*)w;
}

CC_NOINLINE static void LScreen_Slider(struct LScreen* s, struct LSlider* w, int width, int height,
									  int initValue, int maxValue, BitmapCol progressCol) {
	LSlider_Init(w, width, height);
	w->Value = initValue; w->MaxValue = maxValue;
	w->ProgressCol = progressCol;
	s->Widgets[s->NumWidgets++] = (struct LWidget*)w;
}

static void SwitchToChooseMode(void* w, int x, int y) {
	Launcher_SetScreen(ChooseModeScreen_MakeInstance(false));
}
static void SwitchToColours(void* w, int x, int y) {
	Launcher_SetScreen(ColoursScreen_MakeInstance());
}
static void SwitchToDirectConnect(void* w, int x, int y) {
	Launcher_SetScreen(DirectConnectScreen_MakeInstance());
}
static void SwitchToMain(void* w, int x, int y) {
	Launcher_SetScreen(MainScreen_MakeInstance());
}
static void SwitchToSettings(void* w, int x, int y) {
	Launcher_SetScreen(SettingsScreen_MakeInstance());
}
static void SwitchToUpdates(void* w, int x, int y) {
	Launcher_SetScreen(UpdatesScreen_MakeInstance());
}


/*########################################################################################################################*
*-------------------------------------------------------ChooseModeScreen--------------------------------------------------*
*#########################################################################################################################*/
static struct ChooseModeScreen {
	LScreen_Layout
	struct LButton BtnEnhanced, BtnClassicHax, BtnClassic, BtnBack;
	struct LLabel  LblTitle, LblHelp, LblEnhanced[2], LblClassicHax[2], LblClassic[2];
	bool FirstTime;
	struct LWidget* _widgets[12];
} ChooseModeScreen_Instance;

CC_NOINLINE static void ChooseMode_Click(bool classic, bool classicHacks) {
	Launcher_ClassicBackground = classic;
	Options_Load();
	Options_SetBool(OPT_CLASSIC_MODE, classic);
	if (classic) Options_SetBool(OPT_CLASSIC_HACKS, classicHacks);

	Options_SetBool("nostalgia-classicbg", classic);
	Options_SetBool(OPT_CUSTOM_BLOCKS,   !classic);
	Options_SetBool(OPT_CPE,             !classic);
	Options_SetBool(OPT_SERVER_TEXTURES, !classic);
	Options_SetBool(OPT_CLASSIC_TABLIST, classic);
	Options_SetBool(OPT_CLASSIC_OPTIONS, classic);
	Options_Save();

	Launcher_SetScreen(MainScreen_MakeInstance());
}

static void UseModeEnhanced(void* w, int x, int y)   { ChooseMode_Click(false, false); }
static void UseModeClassicHax(void* w, int x, int y) { ChooseMode_Click(true,  true);  }
static void UseModeClassic(void* w, int x, int y)    { ChooseMode_Click(true,  false); }

static void ChooseModeScreen_Init(struct LScreen* s_) {
	const static String titleText = String_FromConst("Choose game mode");
	struct ChooseModeScreen* s = (struct ChooseModeScreen*)s_;	

	s->LblHelp.Hidden = !s->FirstTime;
	s->BtnBack.Hidden = s->FirstTime;

	if (s->NumWidgets) return;
	s->Widgets = s->_widgets;
	LScreen_Label(s_,  &s->LblTitle, "");

	LScreen_Button(s_, &s->BtnEnhanced, 145, 35, "Enhanced");
	LScreen_Label(s_,  &s->LblEnhanced[0], "&eEnables custom blocks, changing env");
	LScreen_Label(s_,  &s->LblEnhanced[1], "&esettings, longer messages, and more");

	LScreen_Button(s_, &s->BtnClassicHax, 145, 35, "Classic +hax");
	LScreen_Label(s_,  &s->LblClassicHax[0], "&eSame as Classic mode, except that");
	LScreen_Label(s_,  &s->LblClassicHax[1], "&ehacks (noclip/fly/speed) are enabled");

	LScreen_Button(s_, &s->BtnClassic, 145, 35, "Classic");
	LScreen_Label(s_,  &s->LblClassic[0], "&eOnly uses blocks and features from");
	LScreen_Label(s_,  &s->LblClassic[1], "&ethe original minecraft classic");

	LScreen_Label(s_,  &s->LblHelp, "&eClick &fEnhanced &eif you'e not sure which mode to choose.");
	LScreen_Button(s_, &s->BtnBack, 80, 35, "Back");

	s->BtnEnhanced.OnClick   = UseModeEnhanced;
	s->BtnClassicHax.OnClick = UseModeClassicHax;
	s->BtnClassic.OnClick    = UseModeClassic;
	s->BtnBack.OnClick       = SwitchToSettings;

	s->LblTitle.Font = Launcher_TitleFont;
	LLabel_SetText(&s->LblTitle, &titleText);
}

static void ChooseModeScreen_Reposition(struct LScreen* s_) {
	struct ChooseModeScreen* s = (struct ChooseModeScreen*)s_;
	int middle = Game_Width / 2;
	LWidget_SetLocation(&s->LblTitle, ANCHOR_CENTRE, ANCHOR_CENTRE, 10, -135);

	LWidget_SetLocation(&s->BtnEnhanced,    ANCHOR_MIN, ANCHOR_CENTRE, middle - 250, -72);
	LWidget_SetLocation(&s->LblEnhanced[0], ANCHOR_MIN, ANCHOR_CENTRE, middle - 85,  -72 - 12);
	LWidget_SetLocation(&s->LblEnhanced[1], ANCHOR_MIN, ANCHOR_CENTRE, middle - 85,  -72 + 12);

	LWidget_SetLocation(&s->BtnClassicHax,    ANCHOR_MIN, ANCHOR_CENTRE, middle - 250, 0);
	LWidget_SetLocation(&s->LblClassicHax[0], ANCHOR_MIN, ANCHOR_CENTRE, middle - 85,  0 - 12);
	LWidget_SetLocation(&s->LblClassicHax[1], ANCHOR_MIN, ANCHOR_CENTRE, middle - 85,  0 + 12);

	LWidget_SetLocation(&s->BtnClassic,    ANCHOR_MIN, ANCHOR_CENTRE, middle - 250, 72);
	LWidget_SetLocation(&s->LblClassic[0], ANCHOR_MIN, ANCHOR_CENTRE, middle - 85,  72 - 12);
	LWidget_SetLocation(&s->LblClassic[1], ANCHOR_MIN, ANCHOR_CENTRE, middle - 85,  72 + 12);

	LWidget_SetLocation(&s->LblHelp, ANCHOR_CENTRE, ANCHOR_CENTRE, 0, 160);
	LWidget_SetLocation(&s->BtnBack, ANCHOR_CENTRE, ANCHOR_CENTRE, 0, 170);
}

static void ChooseModeScreen_Draw(struct LScreen* s) {
	int midX = Game_Width / 2, midY = Game_Height / 2;
	LScreen_Draw(s);
	
	Drawer2D_Rect(&Launcher_Framebuffer, Launcher_ButtonBorderCol,
					midX - 250, midY - 35, 490, 1);
	Drawer2D_Rect(&Launcher_Framebuffer, Launcher_ButtonBorderCol, 
					midX - 250, midY + 35, 490, 1);
}

struct LScreen* ChooseModeScreen_MakeInstance(bool firstTime) {
	struct ChooseModeScreen* s = &ChooseModeScreen_Instance;
	LScreen_Reset((struct LScreen*)s);
	s->Init       = ChooseModeScreen_Init;
	s->Reposition = ChooseModeScreen_Reposition;
	s->Draw       = ChooseModeScreen_Draw;
	s->FirstTime  = firstTime;
	return (struct LScreen*)s;
}


/*########################################################################################################################*
*---------------------------------------------------------ColoursScreen---------------------------------------------------*
*#########################################################################################################################*/
static struct ColoursScreen {
	LScreen_Layout
	struct LButton BtnDefault, BtnBack;
	struct LLabel LblNames[5], LblRGB[3];
	struct LInput IptColours[5 * 3];
	struct LWidget* _widgets[25];
	float ColourAcc;
} ColoursScreen_Instance;

CC_NOINLINE static void ColoursScreen_Update(struct ColoursScreen* s, int i, BitmapCol col) {
	String tmp; char tmpBuffer[3];

	String_InitArray(tmp, tmpBuffer);
	String_AppendInt(&tmp, col.R);
	LInput_SetText(&s->IptColours[i + 0], &tmp);

	tmp.length = 0;
	String_AppendInt(&tmp, col.G);
	LInput_SetText(&s->IptColours[i + 1], &tmp);

	tmp.length = 0;
	String_AppendInt(&tmp, col.B);
	LInput_SetText(&s->IptColours[i + 2], &tmp);
}

CC_NOINLINE static void ColoursScreen_UpdateAll(struct ColoursScreen* s) {
	ColoursScreen_Update(s,  0, Launcher_BackgroundCol);
	ColoursScreen_Update(s,  3, Launcher_ButtonBorderCol);
	ColoursScreen_Update(s,  6, Launcher_ButtonHighlightCol);
	ColoursScreen_Update(s,  9, Launcher_ButtonForeCol);
	ColoursScreen_Update(s, 12, Launcher_ButtonForeActiveCol);
}

static void ColoursScreen_TextChanged(struct LInput* w) {
	struct ColoursScreen* s = &ColoursScreen_Instance;
	int index = LScreen_IndexOf((struct LScreen*)s, w);
	BitmapCol* col;
	uint8_t r, g, b;

	if (index < 3)       col = &Launcher_BackgroundCol;
	else if (index < 6)  col = &Launcher_ButtonBorderCol;
	else if (index < 9)  col = &Launcher_ButtonHighlightCol;
	else if (index < 12) col = &Launcher_ButtonForeCol;
	else                 col = &Launcher_ButtonForeActiveCol;

	/* if index of G input, changes to index of R input */
	index = (index / 3) * 3;
	if (!Convert_ParseUInt8(&s->IptColours[index + 0].Text, &r)) return;
	if (!Convert_ParseUInt8(&s->IptColours[index + 1].Text, &g)) return;
	if (!Convert_ParseUInt8(&s->IptColours[index + 2].Text, &b)) return;

	Launcher_SaveSkin();
	col->R = r; col->G = g; col->B = b;
	Launcher_Redraw();
}

static void ColoursScreen_AdjustSelected(struct LScreen* s, int delta) {
	struct LInput* w;
	int index, newCol;
	uint8_t col;

	if (!s->SelectedWidget) return;
	index = LScreen_IndexOf(s, s->SelectedWidget);
	if (index >= 15) return;

	w = (struct LInput*)s->SelectedWidget;
	if (!Convert_ParseUInt8(&w->Text, &col)) return;
	newCol = col + delta;

	Math_Clamp(newCol, 0, 255);
	w->Text.length = 0;
	String_AppendInt(&w->Text, newCol);

	if (w->CaretPos >= w->Text.length) w->CaretPos = -1;
	ColoursScreen_TextChanged(w);
}

static void ColoursScreen_MouseWheel(struct LScreen* s_, float delta) {
	struct ColoursScreen* s = (struct ColoursScreen*)s_;
	int steps = Utils_AccumulateWheelDelta(&s->ColourAcc, delta);
	ColoursScreen_AdjustSelected(s_, steps);
}

static void ColoursScreen_KeyDown(struct LScreen* s, Key key) {
	if (key == KEY_LEFT) {
		ColoursScreen_AdjustSelected(s, -1);
	} else if (key == KEY_RIGHT) {
		ColoursScreen_AdjustSelected(s, +1);
	} else if (key == KEY_UP) {
		ColoursScreen_AdjustSelected(s, +10);
	} else if (key == KEY_DOWN) {
		ColoursScreen_AdjustSelected(s, -10);
	} else {
		LScreen_KeyDown(s, key);
	}
}

static void ColoursScreen_ResetAll(void* w, int x, int y) {
	Launcher_ResetSkin();
	Launcher_SaveSkin();
	ColoursScreen_UpdateAll(&ColoursScreen_Instance);
	Launcher_Redraw();
}

static void ColoursScreen_Init(struct LScreen* s_) {
	struct ColoursScreen* s = (struct ColoursScreen*)s_;
	int i;

	s->ColourAcc = 0;
	if (s->NumWidgets) return;
	s->Widgets = s->_widgets;
	
	for (i = 0; i < 5 * 3; i++) {
		LScreen_Input(s_, &s->IptColours[i], 55, false, NULL);
		s->IptColours[i].TextChanged = ColoursScreen_TextChanged;
	}

	LScreen_Label(s_, &s->LblNames[0], "Background");
	LScreen_Label(s_, &s->LblNames[1], "Button border");
	LScreen_Label(s_, &s->LblNames[2], "Button highlight");
	LScreen_Label(s_, &s->LblNames[3], "Button");
	LScreen_Label(s_, &s->LblNames[4], "Active button");

	LScreen_Label(s_, &s->LblRGB[0], "Red");
	LScreen_Label(s_, &s->LblRGB[1], "Green");
	LScreen_Label(s_, &s->LblRGB[2], "Blue");

	LScreen_Button(s_, &s->BtnDefault, 160, 35, "Default colours");
	LScreen_Button(s_, &s->BtnBack,    80,  35, "Back");

	s->BtnDefault.OnClick = ColoursScreen_ResetAll;
	s->BtnBack.OnClick    = SwitchToSettings;
	ColoursScreen_UpdateAll(s);
}

static void ColoursScreen_Reposition(struct LScreen* s_) {
	struct ColoursScreen* s = (struct ColoursScreen*)s_;
	int i, y;
	for (i = 0; i < 5; i++) {
		y = -100 + 40 * i;
		LWidget_SetLocation(&s->IptColours[i*3 + 0], ANCHOR_CENTRE, ANCHOR_CENTRE,  30, y);
		LWidget_SetLocation(&s->IptColours[i*3 + 1], ANCHOR_CENTRE, ANCHOR_CENTRE,  95, y);
		LWidget_SetLocation(&s->IptColours[i*3 + 2], ANCHOR_CENTRE, ANCHOR_CENTRE, 160, y);
	}

	LWidget_SetLocation(&s->LblNames[0], ANCHOR_CENTRE, ANCHOR_CENTRE, -60, -100);
	LWidget_SetLocation(&s->LblNames[1], ANCHOR_CENTRE, ANCHOR_CENTRE, -70,  -60);
	LWidget_SetLocation(&s->LblNames[2], ANCHOR_CENTRE, ANCHOR_CENTRE, -80,  -20);
	LWidget_SetLocation(&s->LblNames[3], ANCHOR_CENTRE, ANCHOR_CENTRE, -40,   20);
	LWidget_SetLocation(&s->LblNames[4], ANCHOR_CENTRE, ANCHOR_CENTRE, -70,   60);

	LWidget_SetLocation(&s->LblRGB[0], ANCHOR_CENTRE, ANCHOR_CENTRE,  30, -130);
	LWidget_SetLocation(&s->LblRGB[1], ANCHOR_CENTRE, ANCHOR_CENTRE,  95, -130);
	LWidget_SetLocation(&s->LblRGB[2], ANCHOR_CENTRE, ANCHOR_CENTRE, 160, -130);

	LWidget_SetLocation(&s->BtnDefault, ANCHOR_CENTRE, ANCHOR_CENTRE, 0, 120);
	LWidget_SetLocation(&s->BtnBack,    ANCHOR_CENTRE, ANCHOR_CENTRE, 0, 170);
}

struct LScreen* ColoursScreen_MakeInstance(void) {
	struct ColoursScreen* s = &ColoursScreen_Instance;
	LScreen_Reset((struct LScreen*)s);
	s->Init       = ColoursScreen_Init;
	s->Reposition = ColoursScreen_Reposition;
	s->KeyDown    = ColoursScreen_KeyDown;
	s->MouseWheel = ColoursScreen_MouseWheel;
	return (struct LScreen*)s;
}


/*########################################################################################################################*
*------------------------------------------------------DirectConnectScreen------------------------------------------------*
*#########################################################################################################################*/
static struct DirectConnectScreen {
	LScreen_Layout
	struct LButton BtnConnect, BtnBack;
	struct LInput IptUsername, IptAddress, IptMppass;
	struct LLabel LblStatus;
	struct LWidget* _widgets[6];
} DirectConnectScreen_Instance;

CC_NOINLINE static void DirectConnectScreen_SetStatus(const char* msg) {
	String str = String_FromReadonly(msg);
	struct LLabel* w = &DirectConnectScreen_Instance.LblStatus;

	LLabel_SetText(w, &str);
	LWidget_Redraw(w);
}

static void DirectConnectScreen_Load(struct DirectConnectScreen* s) {
	String addr; char addrBuffer[STRING_SIZE];
	String mppass; char mppassBuffer[STRING_SIZE];
	String user, ip, port;
	Options_Load();

	Options_UNSAFE_Get("launcher-dc-username", &user);
	Options_UNSAFE_Get("launcher-dc-ip",       &ip);
	Options_UNSAFE_Get("launcher-dc-port",     &port);
	
	String_InitArray(mppass, mppassBuffer);
	Options_GetSecure("launcher-dc-mppass", &mppass, &user);
	String_InitArray(addr, addrBuffer);
	String_Format2(&addr, "%s:%s", &ip, &port);

	/* don't want just ':' for address */
	if (addr.length == 1) addr.length = 0;
	
	LInput_SetText(&s->IptUsername, &user);
	LInput_SetText(&s->IptAddress,  &addr);
	LInput_SetText(&s->IptMppass,   &mppass);
}

static void DirectConnectScreen_Save(const String* user, const String* mppass, const String* ip, const String* port) {
	Options_Load();
	Options_Set("launcher-dc-username", user);
	Options_Set("launcher-dc-ip",       ip);
	Options_Set("launcher-dc-port",     port);
	Options_SetSecure("launcher-dc-mppass", mppass, user);
}

static void DirectConnectScreen_StartClient(void* w, int x, int y) {
	const static String loopbackIp = String_FromConst("127.0.0.1");
	const static String defMppass  = String_FromConst("(none)");
	const String* user   = &DirectConnectScreen_Instance.IptUsername.Text;
	const String* addr   = &DirectConnectScreen_Instance.IptAddress.Text;
	const String* mppass = &DirectConnectScreen_Instance.IptMppass.Text;

	String ip, port;
	uint8_t  raw_ip[4];
	uint16_t raw_port;

	int index = String_LastIndexOf(addr, ':');
	if (index <= 0 || index == addr->length - 1) {
		DirectConnectScreen_SetStatus("&eInvalid address"); return;
	}

	ip   = String_UNSAFE_Substring(addr, 0, index);
	port = String_UNSAFE_SubstringAt(addr, index + 1);
	if (String_CaselessEqualsConst(&ip, "localhost")) ip = loopbackIp;

	if (!user->length) {
		DirectConnectScreen_SetStatus("&eUsername required"); return;
	}
	if (!Utils_ParseIP(&ip, raw_ip)) {
		DirectConnectScreen_SetStatus("&eInvalid ip"); return;
	}
	if (!Convert_ParseUInt16(&port, &raw_port)) {
		DirectConnectScreen_SetStatus("&eInvalid port"); return;
	}
	if (!mppass->length) mppass = &defMppass;

	DirectConnectScreen_Save(user, mppass, &ip, &port);
	DirectConnectScreen_SetStatus("");
	Launcher_StartGame(user, mppass, &ip, &port, &String_Empty);
}

static void DirectConnectScreen_Init(struct LScreen* s_) {
	struct DirectConnectScreen* s = (struct DirectConnectScreen*)s_;
	if (s->NumWidgets) return;
	s->Widgets = s->_widgets;

	LScreen_Input(s_, &s->IptUsername, 330, false, "&gUsername..");
	LScreen_Input(s_, &s->IptAddress,  330, false, "&gIP address:Port number..");
	LScreen_Input(s_, &s->IptMppass,   330, false, "&gMppass..");

	LScreen_Button(s_, &s->BtnConnect, 110, 35, "Connect");
	LScreen_Button(s_, &s->BtnBack,     80, 35, "Back");
	LScreen_Label(s_,  &s->LblStatus, "");

	s->BtnConnect.OnClick = DirectConnectScreen_StartClient;
	s->BtnBack.OnClick    = SwitchToMain;
	/* Init input text from options */
	DirectConnectScreen_Load(s);
}

static void DirectConnectScreen_Reposition(struct LScreen* s_) {
	struct DirectConnectScreen* s = (struct DirectConnectScreen*)s_;
	LWidget_SetLocation(&s->IptUsername, ANCHOR_CENTRE, ANCHOR_CENTRE, 0, -120);
	LWidget_SetLocation(&s->IptAddress,  ANCHOR_CENTRE, ANCHOR_CENTRE, 0,  -75);
	LWidget_SetLocation(&s->IptMppass,   ANCHOR_CENTRE, ANCHOR_CENTRE, 0,  -30);

	LWidget_SetLocation(&s->BtnConnect, ANCHOR_CENTRE, ANCHOR_CENTRE, -110, 20);
	LWidget_SetLocation(&s->BtnBack,    ANCHOR_CENTRE, ANCHOR_CENTRE,  125, 20);
	LWidget_SetLocation(&s->LblStatus,  ANCHOR_CENTRE, ANCHOR_CENTRE,    0, 70);
}

struct LScreen* DirectConnectScreen_MakeInstance(void) {
	struct DirectConnectScreen* s = &DirectConnectScreen_Instance;
	LScreen_Reset((struct LScreen*)s);
	s->Init          = DirectConnectScreen_Init;
	s->Reposition    = DirectConnectScreen_Reposition;
	s->OnEnterWidget = (struct LWidget*)&s->BtnConnect;
	return (struct LScreen*)s;
}


/*########################################################################################################################*
*----------------------------------------------------------MainScreen-----------------------------------------------------*
*#########################################################################################################################*/
static struct MainScreen {
	LScreen_Layout
	struct LButton BtnLogin, BtnResume, BtnDirect, BtnSPlayer, BtnOptions;
	struct LInput IptUsername, IptPassword;
	struct LLabel LblStatus, LblUpdate;
	struct LWidget* _widgets[9];
	bool SigningIn;
} MainScreen_Instance;

struct ResumeInfo {
	String User, Ip, Port, Server, Mppass;
	char _mppassBuffer[STRING_SIZE];
	bool Valid;
};

CC_NOINLINE static void MainScreen_UNSAFE_GetResume(struct ResumeInfo* info, bool full) {
	Options_UNSAFE_Get("launcher-server",   &info->Server);
	Options_UNSAFE_Get("launcher-username", &info->User);
	Options_UNSAFE_Get("launcher-ip",       &info->Ip);
	Options_UNSAFE_Get("launcher-port",     &info->Port);

	if (!full) return;
	String_InitArray(info->Mppass, info->_mppassBuffer);
	Options_GetSecure("launcher-mppass", &info->Mppass, &info->User);

	info->Valid = 
		info->User.length && info->Mppass.length &&
		info->Ip.length   && info->Port.length;
}

CC_NOINLINE void MainScreen_Error(struct LWebTask* task, const char* action) {
	String str; char strBuffer[STRING_SIZE];
	struct MainScreen* s = &MainScreen_Instance;

	String_InitArray(str, strBuffer);
	s->SigningIn = false;

	if (task->Status) {
		String_Format1(&str, "&cclassicube.net returned: %i error", &task->Status);
	} else {
		String_Format1(&str, "&cError %i when connecting to classicube.net", &task->Res);
	}
	LLabel_SetText(&s->LblStatus, &str);
	LWidget_Redraw(&s->LblStatus);
}

static void MainScreen_Login(void* w, int x, int y) {
	const static String needUser = String_FromConst("&eUsername required");
	const static String needPass = String_FromConst("&ePassword required");
	const static String signIn   = String_FromConst("&e&eSigning in..");

	struct MainScreen* s = &MainScreen_Instance;
	String* user = &s->IptUsername.Text;
	String* pass = &s->IptPassword.Text;

	if (!user->length) {
		LLabel_SetText(&s->LblStatus, &needUser);
		LWidget_Redraw(&s->LblStatus); return;
	}
	if (!pass->length) {
		LLabel_SetText(&s->LblStatus, &needPass);
		LWidget_Redraw(&s->LblStatus); return;
	}

	if (GetTokenTask.Base.Working) return;
	Options_Set("launcher-cc-username", user);
	Options_SetSecure("launcher-cc-password", pass, user);

	GetTokenTask_Run();
	LLabel_SetText(&s->LblStatus, &signIn);
	LWidget_Redraw(&s->LblStatus);
	s->SigningIn = true;
}

static void MainScreen_Resume(void* w, int x, int y) {
	struct ResumeInfo info;
	MainScreen_UNSAFE_GetResume(&info, true);

	if (!info.Valid) return;
	Launcher_StartGame(&info.User, &info.Mppass, &info.Ip, &info.Port, &info.Server);
}

static void MainScreen_Singleplayer(void* w, int x, int y) {
	const static String defUser = String_FromConst("Singleplayer");
	const String* user = &MainScreen_Instance.IptUsername.Text;

	if (!user->length) user = &defUser;
	Launcher_StartGame(user, &String_Empty, &String_Empty, &String_Empty, &String_Empty);
}

static void MainScreen_Init(struct LScreen* s_) {
	String user, pass; char passBuffer[STRING_SIZE];
	const static String update = String_FromConst("&eChecking..");
	struct MainScreen* s = (struct MainScreen*)s_;

	if (s->NumWidgets) return;
	s->Widgets = s->_widgets;

	LScreen_Input(s_, &s->IptUsername, 280, false, "&gUsername..");
	LScreen_Input(s_, &s->IptPassword, 280, true,  "&gPassword..");

	LScreen_Button(s_, &s->BtnLogin, 100, 35, "Sign in");
	LScreen_Label(s_,  &s->LblStatus, "");

	LScreen_Button(s_, &s->BtnResume,  100, 35, "Resume");
	LScreen_Button(s_, &s->BtnDirect,  200, 35, "Direct connect");
	LScreen_Button(s_, &s->BtnSPlayer, 200, 35, "Singleplayer");

	LScreen_Label(s_,  &s->LblUpdate, "");
	LScreen_Button(s_, &s->BtnOptions, 100, 35, "Options");
	
	s->BtnLogin.OnClick   = MainScreen_Login;
	s->BtnResume.OnClick  = MainScreen_Resume;
	s->BtnDirect.OnClick  = SwitchToDirectConnect;
	s->BtnSPlayer.OnClick = MainScreen_Singleplayer;
	s->BtnOptions.OnClick = SwitchToSettings;

	/* need to set text here for right size */
	s->LblUpdate.Font = Launcher_HintFont;
	LLabel_SetText(&s->LblUpdate, &update);
	
	String_InitArray(pass, passBuffer);
	Options_UNSAFE_Get("launcher-cc-username", &user);
	Options_GetSecure("launcher-cc-password", &pass, &user);

	LInput_SetText(&s->IptUsername, &user);
	LInput_SetText(&s->IptPassword, &pass);
}

static void MainScreen_Reposition(struct LScreen* s_) {
	struct MainScreen* s = (struct MainScreen*)s_;
	LWidget_SetLocation(&s->IptUsername, ANCHOR_CENTRE, ANCHOR_CENTRE, 0, -120);
	LWidget_SetLocation(&s->IptPassword, ANCHOR_CENTRE, ANCHOR_CENTRE, 0,  -75);

	LWidget_SetLocation(&s->BtnLogin,  ANCHOR_CENTRE, ANCHOR_CENTRE, -90, -25);
	LWidget_SetLocation(&s->LblStatus, ANCHOR_CENTRE, ANCHOR_CENTRE,   0,  20);

	LWidget_SetLocation(&s->BtnResume,  ANCHOR_CENTRE, ANCHOR_CENTRE, 90, -25);
	LWidget_SetLocation(&s->BtnDirect,  ANCHOR_CENTRE, ANCHOR_CENTRE,  0,  60);
	LWidget_SetLocation(&s->BtnSPlayer, ANCHOR_CENTRE, ANCHOR_CENTRE,  0, 110);

	LWidget_SetLocation(&s->LblUpdate,  ANCHOR_MAX, ANCHOR_MAX,  10,  45);
	LWidget_SetLocation(&s->BtnOptions, ANCHOR_MAX, ANCHOR_MAX,   6,   6);
}

static void MainScreen_HoverWidget(struct LScreen* s_, struct LWidget* w) {
	String str; char strBuffer[STRING_SIZE];
	struct MainScreen* s = (struct MainScreen*)s_;
	struct ResumeInfo info;

	LScreen_HoverWidget(s_, w);
	if (s->SigningIn || w != (struct LWidget*)&s->BtnResume) return;

	MainScreen_UNSAFE_GetResume(&info, false);
	if (!info.User.length) return;
	String_InitArray(str, strBuffer);

	if (info.Server.length && String_Equals(&info.User,	&s->IptUsername.Text)) {
		String_Format1(&str, "&eResume to %s", &info.Server);
	} else if (info.Server.length) {
		String_Format2(&str, "&eResume as %s to %s", &info.User, &info.Server);
	} else {
		String_Format3(&str, "&eResume as %s to %s:%s", &info.User, &info.Ip, &info.Port);
	}

	LLabel_SetText(&s->LblStatus, &str);
	LWidget_Redraw(&s->LblStatus);
}

static void MainScreen_UnhoverWidget(struct LScreen* s_, struct LWidget* w) {
	struct MainScreen* s = (struct MainScreen*)s_;
	LScreen_UnhoverWidget(s_, w);
	if (s->SigningIn || w != (struct LWidget*)&s->BtnResume) return;

	LLabel_SetText(&s->LblStatus, &String_Empty);
	LWidget_Redraw(&s->LblStatus);
}

/* TODO: Maybe find a not so hacky way.. */
CC_NOINLINE static uint32_t MainScreen_GetVersion(const String* version) {
	uint8_t raw[4];
	if (!Utils_ParseIP(version, raw)) return 0;
	return Stream_GetU32_BE(raw);
}

static void MainScreen_TickCheckUpdates(struct MainScreen* s) {
	const static String needUpdate = String_FromConst("&aNew release");
	const static String upToDate   = String_FromConst("&eUp to date");
	const static String failed     = String_FromConst("&cCheck failed");
	const static String currentStr = String_FromConst(PROGRAM_APP_VER);
	uint32_t latest, current;

	if (!CheckUpdateTask.Base.Working)   return;
	LWebTask_Tick(&CheckUpdateTask.Base);
	if (!CheckUpdateTask.Base.Completed) return;

	if (CheckUpdateTask.Base.Success) {
		latest  = MainScreen_GetVersion(&CheckUpdateTask.LatestRelease);
		current = MainScreen_GetVersion(&currentStr);
		LLabel_SetText(&s->LblUpdate, latest > current ? &needUpdate : &upToDate);
	} else {
		LLabel_SetText(&s->LblUpdate, &failed);
	}
	LWidget_Redraw(&s->LblUpdate);
}

static void MainScreen_TickGetToken(struct MainScreen* s) {
	if (!GetTokenTask.Base.Working)   return;
	LWebTask_Tick(&GetTokenTask.Base);
	if (!GetTokenTask.Base.Completed) return;

	if (GetTokenTask.Base.Success) {
		SignInTask_Run(&s->IptUsername.Text, &s->IptPassword.Text);
	} else {
		MainScreen_Error(&GetTokenTask.Base, "sign in");
	}
}

static void MainScreen_TickSignIn(struct MainScreen* s) {
	const static String fetchMsg = String_FromConst("&eRetrieving servers list..");
	if (!SignInTask.Base.Working)   return;
	LWebTask_Tick(&SignInTask.Base);
	if (!SignInTask.Base.Completed) return;

	if (SignInTask.Error.length) {
		LLabel_SetText(&s->LblStatus, &SignInTask.Error);
		LWidget_Redraw(&s->LblStatus);
	} else if (SignInTask.Base.Success) {
		/* website returns case correct username */
		if (!String_Equals(&s->IptUsername.Text, &SignInTask.Username)) {
			LInput_SetText(&s->IptUsername, &SignInTask.Username);
			LWidget_Redraw(&s->IptUsername);
		}

		FetchServersTask_Run();
		LLabel_SetText(&s->LblStatus, &fetchMsg);
		LWidget_Redraw(&s->LblStatus);
	} else {
		MainScreen_Error(&SignInTask.Base, "sign in");
	}
}

static void MainScreen_TickFetchServers(struct MainScreen* s) {
	if (!FetchServersTask.Base.Working)   return;
	LWebTask_Tick(&FetchServersTask.Base);
	if (!FetchServersTask.Base.Completed) return;

	if (FetchServersTask.Base.Success) {
		s->SigningIn = false;
		Launcher_SetScreen(ServersScreen_MakeInstance());
	} else {
		MainScreen_Error(&FetchServersTask.Base, "retrieving servers list");
	}
}

static void MainScreen_Tick(struct LScreen* s_) {
	struct MainScreen* s = (struct MainScreen*)s_;
	LScreen_Tick(s_);

	MainScreen_TickCheckUpdates(s);
	MainScreen_TickGetToken(s);
	MainScreen_TickSignIn(s);
	MainScreen_TickFetchServers(s);
}

struct LScreen* MainScreen_MakeInstance(void) {
	struct MainScreen* s = &MainScreen_Instance;
	LScreen_Reset((struct LScreen*)s);
	s->Init       = MainScreen_Init;
	s->Tick       = MainScreen_Tick;
	s->Reposition = MainScreen_Reposition;
	s->HoverWidget   = MainScreen_HoverWidget;
	s->UnhoverWidget = MainScreen_UnhoverWidget;
	s->OnEnterWidget = (struct LWidget*)&s->BtnLogin;
	return (struct LScreen*)s;
}


/*########################################################################################################################*
*-------------------------------------------------------ResourcesScreen---------------------------------------------------*
*#########################################################################################################################*/
static struct ResourcesScreen {
	LScreen_Layout
	struct LLabel  LblLine1, LblLine2, LblStatus;
	struct LButton BtnYes, BtnNo, BtnCancel;
	struct LSlider SdrProgress;
	struct LWidget* _widgets[7];
} ResourcesScreen_Instance;

static void ResourcesScreen_Download(void* w, int x, int y) {
	struct ResourcesScreen* s = &ResourcesScreen_Instance;
	if (Fetcher_Working) return;

	Fetcher_Run();
	s->SelectedWidget = NULL;

	s->BtnYes.Hidden   = true;
	s->BtnNo.Hidden    = true;
	s->LblLine1.Hidden = true;
	s->LblLine2.Hidden = true;
	s->BtnCancel.Hidden   = false;
	s->SdrProgress.Hidden = false;
	s->Draw((struct LScreen*)s);
}

static void ResourcesScreen_Next(void* w, int x, int y) {
	const static String optionsTxt = String_FromConst("options.txt");
	AsyncDownloader_Clear();

	if (File_Exists(&optionsTxt)) {
		Launcher_SetScreen(MainScreen_MakeInstance());
	} else {
		Launcher_SetScreen(ChooseModeScreen_MakeInstance(true));
	}
}

static void ResourcesScreen_Init(struct LScreen* s_) {
	String str; char buffer[STRING_SIZE];
	BitmapCol progressCol = BITMAPCOL_CONST(0, 220, 0, 255);
	struct ResourcesScreen* s = (struct ResourcesScreen*)s_;
	float size;

	if (s->NumWidgets) return;
	s->Widgets = s->_widgets;

	LScreen_Label(s_, &s->LblLine1, "Some required resources weren't found");
	LScreen_Label(s_, &s->LblLine2, "Okay to download?");
	LScreen_Label(s_, &s->LblStatus, "");
	
	LScreen_Button(s_, &s->BtnYes, 70, 35, "Yes");
	LScreen_Button(s_, &s->BtnNo,  70, 35, "No");

	LScreen_Button(s_, &s->BtnCancel, 120, 35, "Cancel");
	LScreen_Slider(s_, &s->SdrProgress, 200, 10, 0, 100, progressCol);

	s->BtnCancel.Hidden   = true;
	s->SdrProgress.Hidden = true;

	/* TODO: Size 13 italic font?? does it matter?? */
	String_InitArray(str, buffer);
	size = Resources_Size / 1024.0f;

	s->LblStatus.Font = Launcher_HintFont;
	String_Format1(&str, "&eDownload size: %f2 megabytes", &size);
	LLabel_SetText(&s->LblStatus, &str);

	s->BtnYes.OnClick    = ResourcesScreen_Download;
	s->BtnNo.OnClick     = ResourcesScreen_Next;
	s->BtnCancel.OnClick = ResourcesScreen_Next;
}

static void ResourcesScreen_Reposition(struct LScreen* s_) {
	struct ResourcesScreen* s = (struct ResourcesScreen*)s_;
	
	LWidget_SetLocation(&s->LblLine1,  ANCHOR_CENTRE, ANCHOR_CENTRE, 0, -50);
	LWidget_SetLocation(&s->LblLine2,  ANCHOR_CENTRE, ANCHOR_CENTRE, 0, -30);
	LWidget_SetLocation(&s->LblStatus, ANCHOR_CENTRE, ANCHOR_CENTRE, 0,  10);

	LWidget_SetLocation(&s->BtnYes, ANCHOR_CENTRE, ANCHOR_CENTRE, -70, 45);
	LWidget_SetLocation(&s->BtnNo,  ANCHOR_CENTRE, ANCHOR_CENTRE,  70, 45);

	LWidget_SetLocation(&s->BtnCancel,   ANCHOR_CENTRE, ANCHOR_CENTRE, 0, 45);
	LWidget_SetLocation(&s->SdrProgress, ANCHOR_CENTRE, ANCHOR_CENTRE, 0, 15);
}

#define RESOURCES_XSIZE 190
#define RESOURCES_YSIZE 70
static void ResourcesScreen_Draw(struct LScreen* s) {
	BitmapCol backCol = BITMAPCOL_CONST( 12, 12,  12, 255);
	BitmapCol boxCol  = BITMAPCOL_CONST(120, 85, 151, 255);

	Drawer2D_Clear(&Launcher_Framebuffer, backCol, 
					0, 0, Game_Width, Game_Height);
	Gradient_Noise(&Launcher_Framebuffer, boxCol, 4,
		Game_Width / 2 - RESOURCES_XSIZE, Game_Height / 2 - RESOURCES_YSIZE,
		RESOURCES_XSIZE * 2,              RESOURCES_YSIZE * 2);
	LScreen_Draw(s);
}

static void ResourcesScreen_SetStatus(const String* str) {
	BitmapCol boxCol = BITMAPCOL_CONST(120, 85, 151, 255);
	struct LLabel* w = &ResourcesScreen_Instance.LblStatus;

	Drawer2D_Clear(&Launcher_Framebuffer, boxCol,
		w->X, w->Y, w->Width, w->Height);
	LLabel_SetText(w, str);
	w->YOffset = -10;
	LWidget_CalcPosition(w);
	LWidget_Draw(w);
}

static void ResourcesScreen_UpdateStatus(struct AsyncRequest* req) {
	String str; char strBuffer[STRING_SIZE];
	String id;
	BitmapCol boxCol = BITMAPCOL_CONST(120, 85, 151, 255);
	struct LLabel* w = &ResourcesScreen_Instance.LblStatus;
	int count;

	id = String_FromRawArray(req->ID);
	String_InitArray(str, strBuffer);
	count = Fetcher_Downloaded + 1;
	String_Format3(&str, "&eFetching %s.. (%i/%i)", &id, &count, &Resources_Count);

	if (String_Equals(&str, &w->Text)) return;
	ResourcesScreen_SetStatus(&str);
}

static void ResourcesScreen_UpdateProgress(struct ResourcesScreen* s) {
	struct AsyncRequest req;
	int progress;

	if (!AsyncDownloader_GetCurrent(&req, &progress)) return;
	ResourcesScreen_UpdateStatus(&req);
	/* making request still, haven't started download yet */
	if (progress < 0 || progress > 100) return;

	if (progress == s->SdrProgress.Value) return;
	s->SdrProgress.Value = progress;
	s->SdrProgress.Hidden = false;
	s->SdrProgress.VTABLE->Draw(&s->SdrProgress);
}

static void ResourcesScreen_Error(struct ResourcesScreen* s) {
	String str; char buffer[STRING_SIZE];
	String_InitArray(str, buffer);

	if (Fetcher_Error) {
		String_Format1(&str, "&cError %h when downloading resources", &Fetcher_Error);
	} else {
		String_Format1(&str, "&c%i error when downloading resources", &Fetcher_StatusCode);
	}
	ResourcesScreen_SetStatus(&str);
}

static void ResourcesScreen_Tick(struct LScreen* s_) {
	struct ResourcesScreen* s = (struct ResourcesScreen*)s_;
	if (!Fetcher_Working) return;

	ResourcesScreen_UpdateProgress(s);
	Fetcher_Update();
	if (!Fetcher_Completed) return;

	if (Fetcher_Error || Fetcher_StatusCode) { 
		ResourcesScreen_Error(s); return; 
	}

	Launcher_TryLoadTexturePack();
	ResourcesScreen_Next(NULL, 0, 0);
}

struct LScreen* ResourcesScreen_MakeInstance(void) {
	struct ResourcesScreen* s = &ResourcesScreen_Instance;
	LScreen_Reset((struct LScreen*)s);
	s->Init       = ResourcesScreen_Init;
	s->Draw       = ResourcesScreen_Draw;
	s->Tick       = ResourcesScreen_Tick;
	s->Reposition = ResourcesScreen_Reposition;
	s->OnEnterWidget = (struct LWidget*)&s->BtnYes;
	return (struct LScreen*)s;
}



/*########################################################################################################################*
*--------------------------------------------------------ServersScreen----------------------------------------------------*
*#########################################################################################################################*/
static struct ServersScreen {
	LScreen_Layout
	struct LInput IptName, IptHash;
	struct LButton BtnBack, BtnConnect, BtnRefresh;
	struct LTable Table;
	struct LWidget* _widgets[6];
	FontDesc RowFont;
} ServersScreen_Instance;

static void ServersScreen_Connect(void* w, int x, int y) {
	Launcher_ConnectToServer(&ServersScreen_Instance.IptHash.Text);
}

static void ServersScreen_Refresh(void* w, int x, int y) {
	const static String working = String_FromConst("&eWorking..");
	struct LButton* btn;
	if (FetchServersTask.Base.Working) return;

	FetchServersTask_Run();
	btn = &ServersScreen_Instance.BtnRefresh;
	LButton_SetText(btn, &working);
	LWidget_Redraw(btn);
}

static void ServersScreen_Init(struct LScreen* s_) {
	struct ServersScreen* s = (struct ServersScreen*)s_;

	Font_Make(&s->RowFont, &Drawer2D_FontName, 11, FONT_STYLE_NORMAL);
	if (s->NumWidgets) return;
	s->Widgets = s->_widgets;

	LScreen_Input(s_, &s->IptName, 370, false, "&gSearch servers..");
	LScreen_Input(s_, &s->IptHash, 475, false, "&gclassicube.net/server/play/...");

	LScreen_Button(s_, &s->BtnBack,    110, 30, "Back");
	LScreen_Button(s_, &s->BtnConnect, 130, 30, "Connect");
	LScreen_Button(s_, &s->BtnRefresh, 110, 30, "Refresh");

	s->BtnBack.OnClick    = SwitchToMain;
	s->BtnConnect.OnClick = ServersScreen_Connect;
	s->BtnRefresh.OnClick = ServersScreen_Refresh;

	LTable_Init(&s->Table, &Launcher_TextFont, &s->RowFont);
	s->Widgets[s->NumWidgets++] = (struct LWidget*)&s->Table;
}

static void ServersScreen_Free(struct LScreen* s_) {
	struct ServersScreen* s = (struct ServersScreen*)s_;
	Font_Free(&s->RowFont);
}

static void ServersScreen_Reposition(struct LScreen* s_) {
	struct ServersScreen* s = (struct ServersScreen*)s_;
	LWidget_SetLocation(&s->IptName, ANCHOR_MIN, ANCHOR_MIN, 10, 10);
	LWidget_SetLocation(&s->IptHash, ANCHOR_MIN, ANCHOR_MAX, 10, 10);

	LWidget_SetLocation(&s->BtnBack,    ANCHOR_MAX, ANCHOR_MIN,  10, 10);
	LWidget_SetLocation(&s->BtnConnect, ANCHOR_MAX, ANCHOR_MAX,  10, 10);
	LWidget_SetLocation(&s->BtnRefresh, ANCHOR_MAX, ANCHOR_MIN, 135, 10);

	s->Table.Width  = Game_Width - 10;
	s->Table.Height = Game_Height - 100;
	s->Table.Height = max(1, s->Table.Height);

	LWidget_SetLocation(&s->Table, ANCHOR_MIN, ANCHOR_MIN, 10, 50);
	LTable_Reposition(&s->Table);
}

struct LScreen* ServersScreen_MakeInstance(void) {
	struct ServersScreen* s = &ServersScreen_Instance;
	LScreen_Reset((struct LScreen*)s);
	s->HidesTitlebar = true;
	s->Init       = ServersScreen_Init;
	s->Free       = ServersScreen_Free;
	s->Reposition = ServersScreen_Reposition;
	return (struct LScreen*)s;
}


/*########################################################################################################################*
*--------------------------------------------------------SettingsScreen---------------------------------------------------*
*#########################################################################################################################*/
static struct SettingsScreen {
	LScreen_Layout
	struct LButton BtnUpdates, BtnMode, BtnColours, BtnBack;
	struct LLabel  LblUpdates, LblMode, LblColours;
	struct LWidget* _widgets[7];
} SettingsScreen_Instance;

static void SettingsScreen_Init(struct LScreen* s_) {
	struct SettingsScreen* s = (struct SettingsScreen*)s_;

	s->BtnColours.Hidden = Launcher_ClassicBackground;
	s->LblColours.Hidden = Launcher_ClassicBackground;

	if (s->NumWidgets) return;
	s->Widgets = s->_widgets;

	LScreen_Button(s_, &s->BtnUpdates, 110, 35, "Updates");
	LScreen_Label(s_,  &s->LblUpdates, "&eGet the latest stuff");

	LScreen_Button(s_, &s->BtnMode, 110, 35, "Mode");
	LScreen_Label(s_,  &s->LblMode, "&eChange the enabled features");

	LScreen_Button(s_, &s->BtnColours, 110, 35, "Colours");
	LScreen_Label(s_,  &s->LblColours, "&eChange how the launcher looks");

	LScreen_Button(s_, &s->BtnBack, 80, 35, "Back");

	s->BtnMode.OnClick    = SwitchToChooseMode;
	s->BtnUpdates.OnClick = SwitchToUpdates;
	s->BtnColours.OnClick = SwitchToColours;
	s->BtnBack.OnClick    = SwitchToMain;
}

static void SettingsScreen_Reposition(struct LScreen* s_) {
	struct SettingsScreen* s = (struct SettingsScreen*)s_;
	LWidget_SetLocation(&s->BtnUpdates, ANCHOR_CENTRE, ANCHOR_CENTRE, -135, -120);
	LWidget_SetLocation(&s->LblUpdates, ANCHOR_CENTRE, ANCHOR_CENTRE,   10, -120);

	LWidget_SetLocation(&s->BtnMode, ANCHOR_CENTRE, ANCHOR_CENTRE, -135, -70);
	LWidget_SetLocation(&s->LblMode, ANCHOR_CENTRE, ANCHOR_CENTRE,   55, -70);

	LWidget_SetLocation(&s->BtnColours, ANCHOR_CENTRE, ANCHOR_CENTRE, -135, -20);
	LWidget_SetLocation(&s->LblColours, ANCHOR_CENTRE, ANCHOR_CENTRE,   65, -20);

	LWidget_SetLocation(&s->BtnBack, ANCHOR_CENTRE, ANCHOR_CENTRE, 0, 170);
}

struct LScreen* SettingsScreen_MakeInstance(void) {
	struct SettingsScreen* s = &SettingsScreen_Instance;
	LScreen_Reset((struct LScreen*)s);
	s->Init       = SettingsScreen_Init;
	s->Reposition = SettingsScreen_Reposition;
	return (struct LScreen*)s;
}


/*########################################################################################################################*
*--------------------------------------------------------UpdatesScreen----------------------------------------------------*
*#########################################################################################################################*/
static struct UpdatesScreen {
	LScreen_Layout
	struct LButton BtnRel[2], BtnDev[2], BtnBack;
	struct LLabel  LblYour, LblRel, LblDev, LblInfo, LblStatus;
	struct LWidget* _widgets[10];
	const char* BuildName;
	int BuildProgress;
} UpdatesScreen_Instance;

static void UpdatesScreen_Draw(struct LScreen* s) {
	int midX = Game_Width / 2, midY = Game_Height / 2;
	LScreen_Draw(s);

	Drawer2D_Rect(&Launcher_Framebuffer, Launcher_ButtonBorderCol,
				midX - 160, midY - 100, 320, 1);
	Drawer2D_Rect(&Launcher_Framebuffer, Launcher_ButtonBorderCol, 
				midX - 160, midY -   5, 320, 1);
}

CC_NOINLINE static void UpdatesScreen_FormatTime(String* str, char* type, int delta, int unit) {
	delta /= unit;
	String_AppendInt(str, delta);
	String_Append(str, ' ');
	String_AppendConst(str, type);

	if (delta > 1) {
		String_AppendConst(str, "s ago");
	} else {
		String_AppendConst(str, " ago");
	}
}

static void UpdatesScreen_Format(struct LLabel* lbl, const char* prefix, TimeMS time) {
	String str; char buffer[STRING_SIZE];
	TimeMS now;

	String_InitArray(str, buffer);
	String_AppendConst(&str, prefix);

	if (!time) {
		String_AppendConst(&str, "&cCheck failed");
	} else {
		now = DateTime_CurrentUTC_MS();
		int delta = (int)(now - time) / 1000;

		if (delta < SECS_PER_MIN) {
			UpdatesScreen_FormatTime(&str, "second", delta, 1);
		} else if (delta < SECS_PER_HOUR) {
			UpdatesScreen_FormatTime(&str, "minute", delta, SECS_PER_MIN);
		} else if (delta < SECS_PER_DAY) {
			UpdatesScreen_FormatTime(&str, "hour",   delta, SECS_PER_HOUR);
		} else {
			UpdatesScreen_FormatTime(&str, "day",    delta, SECS_PER_DAY);
		}
	}
	LLabel_SetText(lbl, &str);
	LWidget_Redraw(lbl);
}

static void UpdatesScreen_FormatBoth(struct UpdatesScreen* s) {
	UpdatesScreen_Format(&s->LblRel, "Latest release: ",   CheckUpdateTask.RelTimestamp);
	UpdatesScreen_Format(&s->LblDev, "Latest dev build: ", CheckUpdateTask.DevTimestamp);
}

static void UpdatesScreen_Get(bool release, bool d3d9) {
	String str; char strBuffer[STRING_SIZE];
	struct UpdatesScreen* s = &UpdatesScreen_Instance;

	TimeMS time = release ? CheckUpdateTask.RelTimestamp : CheckUpdateTask.DevTimestamp;
	if (!time || FetchUpdateTask.Base.Working) return;
	FetchUpdateTask_Run(release, d3d9);

	if (release && d3d9)   s->BuildName = "&eFetching latest release (Direct3D9)";
	if (release && !d3d9)  s->BuildName = "&eFetching latest release (OpenGL)";
	if (!release && d3d9)  s->BuildName = "&eFetching latest dev build (Direct3D9)";
	if (!release && !d3d9) s->BuildName = "&eFetching latest dev build (OpenGL)";

	s->BuildProgress = -1;
	String_InitArray(str, strBuffer);

	String_Format1(&str, "%c..", s->BuildName);
	LLabel_SetText(&s->LblStatus, &str);
	LWidget_Redraw(&s->LblStatus);
}

static void UpdatesScreen_CheckTick(struct UpdatesScreen* s) {
	if (!CheckUpdateTask.Base.Working) return;
	LWebTask_Tick(&CheckUpdateTask.Base);
	if (!CheckUpdateTask.Base.Completed) return;
	UpdatesScreen_FormatBoth(s);
}

static void UpdatesScreen_UpdateProgress(struct UpdatesScreen* s, struct LWebTask* task) {
	String str; char strBuffer[STRING_SIZE];
	String identifier;
	struct AsyncRequest item;
	int progress;
	if (!AsyncDownloader_GetCurrent(&item, &progress)) return;

	identifier = String_FromRawArray(item.ID);
	if (!String_Equals(&identifier, &task->Identifier)) return;
	if (progress == s->BuildProgress) return;

	s->BuildProgress = progress;
	if (progress < 0 || progress > 100) return;
	String_InitArray(str, strBuffer);

	String_Format2(&str, "%c &a%i%%", s->BuildName, &s->BuildProgress);
	LLabel_SetText(&s->LblStatus, &str);
	LWidget_Redraw(&s->LblStatus);
}

static void UpdatesScreen_FetchTick(struct UpdatesScreen* s) {
	const static String failedMsg = String_FromConst("&cFailed to fetch update");
	if (!FetchUpdateTask.Base.Working) return;
	LWebTask_Tick(&FetchUpdateTask.Base);
	UpdatesScreen_UpdateProgress(s, &FetchUpdateTask.Base);
	if (!FetchUpdateTask.Base.Completed) return;

	if (!FetchUpdateTask.Base.Success) {
		LLabel_SetText(&s->LblStatus, &failedMsg);
		LWidget_Redraw(&s->LblStatus);
	} else {
		/* WebTask handles saving of ClassiCube.update for us */
		Launcher_ShouldExit  = true;
		Launcher_ShouldUpdate = true;
	}
}

static void UpdatesScreen_RelD3D9(void* w, int x, int y)   { UpdatesScreen_Get(true,  true);  }
static void UpdatesScreen_RelOpenGL(void* w, int x, int y) { UpdatesScreen_Get(true,  false); }
static void UpdatesScreen_DevD3D9(void* w, int x, int y)   { UpdatesScreen_Get(false, true);  }
static void UpdatesScreen_DevOpenGL(void* w, int x, int y) { UpdatesScreen_Get(false, false); }

static void UpdatesScreen_Init(struct LScreen* s_) {
	const static String exeName = String_FromConst(GAME_EXE_NAME);
	struct UpdatesScreen* s = (struct UpdatesScreen*)s_;
	TimeMS buildTime;
	ReturnCode res;

	if (s->NumWidgets) { CheckUpdateTask_Run(); return; }
	s->Widgets = s->_widgets;
	LScreen_Label(s_, &s->LblYour, "Your build: (unknown)");

	LScreen_Label(s_,  &s->LblRel, "Latest release: Checking..");
	LScreen_Button(s_, &s->BtnRel[0], 130, 35, "Direct3D 9");
	LScreen_Button(s_, &s->BtnRel[1], 130, 35, "OpenGL");

	LScreen_Label(s_,  &s->LblDev, "Latest dev build: Checking..");
	LScreen_Button(s_, &s->BtnDev[0], 130, 35, "Direct3D 9");
	LScreen_Button(s_, &s->BtnDev[1], 130, 35, "OpenGL");

	LScreen_Label(s_,  &s->LblInfo, "&eDirect3D 9 is recommended for Windows");
	LScreen_Label(s_,  &s->LblStatus, "");
	LScreen_Button(s_, &s->BtnBack, 80, 35, "Back");

	s->BtnRel[0].OnClick = UpdatesScreen_RelD3D9;
	s->BtnRel[1].OnClick = UpdatesScreen_RelOpenGL;
	s->BtnDev[0].OnClick = UpdatesScreen_DevD3D9;
	s->BtnDev[1].OnClick = UpdatesScreen_DevOpenGL;
	s->BtnBack.OnClick   = SwitchToSettings;

	/* Initially fill out with update check result from main menu */
	if (CheckUpdateTask.Base.Completed && CheckUpdateTask.Base.Success) {
		UpdatesScreen_FormatBoth(s);
	}
	CheckUpdateTask_Run();
	
	res = File_GetModifiedTime(&exeName, &buildTime);
	if (res) {
		Launcher_ShowError(res, "getting build time");
	} else {
		UpdatesScreen_Format(&s->LblYour, "Your build: ", buildTime);
	}
}

static void UpdatesScreen_Reposition(struct LScreen* s_) {
	struct UpdatesScreen* s = (struct UpdatesScreen*)s_;
	LWidget_SetLocation(&s->LblYour, ANCHOR_CENTRE, ANCHOR_CENTRE, -5, -120);

	LWidget_SetLocation(&s->LblRel,    ANCHOR_CENTRE, ANCHOR_CENTRE, -20, -75);
	LWidget_SetLocation(&s->BtnRel[0], ANCHOR_CENTRE, ANCHOR_CENTRE, -80, -40);
	LWidget_SetLocation(&s->BtnRel[1], ANCHOR_CENTRE, ANCHOR_CENTRE,  80, -40);

	LWidget_SetLocation(&s->LblDev,    ANCHOR_CENTRE, ANCHOR_CENTRE, -30, 20);
	LWidget_SetLocation(&s->BtnDev[0], ANCHOR_CENTRE, ANCHOR_CENTRE, -80, 55);
	LWidget_SetLocation(&s->BtnDev[1], ANCHOR_CENTRE, ANCHOR_CENTRE,  80, 55);

	LWidget_SetLocation(&s->LblInfo,   ANCHOR_CENTRE, ANCHOR_CENTRE, 0, 105);
	LWidget_SetLocation(&s->LblStatus, ANCHOR_CENTRE, ANCHOR_CENTRE, 0, 130);
	LWidget_SetLocation(&s->BtnBack,   ANCHOR_CENTRE, ANCHOR_CENTRE, 0, 170);
}

static void UpdatesScreen_Tick(struct LScreen* s_) {
	struct UpdatesScreen* s = (struct UpdatesScreen*)s_;
	LScreen_Tick(s_);

	UpdatesScreen_FetchTick(s);
	UpdatesScreen_CheckTick(s);
}

struct LScreen* UpdatesScreen_MakeInstance(void) {
	struct UpdatesScreen* s = &UpdatesScreen_Instance;
	LScreen_Reset((struct LScreen*)s);
	s->Init       = UpdatesScreen_Init;
	s->Draw       = UpdatesScreen_Draw;
	s->Tick       = UpdatesScreen_Tick;
	s->Reposition = UpdatesScreen_Reposition;

	s->BuildName     = NULL;
	s->BuildProgress = -1;
	return (struct LScreen*)s;
}
