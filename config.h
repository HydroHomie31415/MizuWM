/* Taken from https://github.com/djpohly/dwl/issues/466 */
#define COLOR(hex)    { ((hex >> 24) & 0xFF) / 255.0f, \
                        ((hex >> 16) & 0xFF) / 255.0f, \
                        ((hex >> 8) & 0xFF) / 255.0f, \
                        (hex & 0xFF) / 255.0f }
/* appearance */
static const int sloppyfocus               = 1;  /* focus follows mouse */
static const int warpcursor                = 1;  /* keyboard focus changes move the cursor */
static const int bypass_surface_visibility = 0;  /* 1 means idle inhibitors will disable idle tracking even if it's surface isn't visible  */
static const unsigned int borderpx         = 1;  /* border pixel of windows */
static const unsigned int snap             = 32; /* snap pixel */
static const float rootcolor[]             = COLOR(0x222222ff);
static const float bordercolor[]           = COLOR(0x444444ff);
static const float focuscolor[]            = COLOR(0x005577ff);
static const float urgentcolor[]           = COLOR(0xff0000ff);
/* This conforms to the xdg-protocol. Set the alpha to zero to restore the old behavior */
static const float fullscreen_bg[]         = {0.0f, 0.0f, 0.0f, 1.0f}; /* You can also use glsl colors */

/* tagging - TAGCOUNT must be no greater than 31 */
#define TAGCOUNT (9)

/* logging */
static int log_level = WLR_ERROR;

/* Regexes (POSIX extended) on app_id and title, from mango's window rules */
static const Rule rules[] = {
	{ .id = "^(org\\.gnome\\.Calculator|gnome-calculator|galculator|blueman-manager|org\\.gnome\\.Nautilus|xdg-desktop-portal.*|zoom)$",
	  .isfloating = 1 },
	{ .id = "^(firefox|zen)$", .title = "^Picture-in-Picture$", .isfloating = 1 },
	{ .id = "^dev\\.noctalia\\.Noctalia$", .isfloating = 1, .width = 1080, .height = 920 },
	{ .id = "^steam$", .title = "^notificationtoasts_[0-9]+_desktop$",
	  .isfloating = 1, .nofocus = 1, .offx = 100, .offy = 100 },
	/* TODO phase 3: named scratchpads; phase 4: game rules (DESIGN.md 6.7, 6.8) */
};

/* layout(s) */
static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[S]",      scroller }, /* first entry is the default */
	{ "[]=",      tile },
};

/* scroller */
static const float scroller_proportion = 0.5f; /* width of new columns */
static const float scroller_presets[] = { 1.0f, 0.5f }; /* cyclewidth steps */

/* monitors */
/* (x=-1, y=-1) is reserved as an "autoconfigure" monitor position indicator
 * WARNING: negative values other than (-1, -1) cause problems with Xwayland clients due to
 * https://gitlab.freedesktop.org/xorg/xserver/-/issues/899 */
static const MonitorRule monrules[] = {
   /* name        mfact  nmaster scale layout       rotate/reflect                x    y
    * example of a HiDPI laptop monitor:
    { "eDP-1",    0.5f,  1,      2,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 }, */
	{ NULL,       0.55f, 1,      1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 },
	/* default monitor rule: can be changed but cannot be eliminated; at least one monitor rule must exist */
};

/* keyboard */
static const struct xkb_rule_names xkb_rules = {
	/* can specify fields: rules, model, layout, variant, options */
	.model = "pc105+inet",
	.layout = "us",
	.options = "terminate:ctrl_alt_bksp",
};
static const int numlock = 1; /* lock Num_Lock on every keyboard at startup */

static const int repeat_rate = 25;
static const int repeat_delay = 600;

/* Trackpad */
static const int tap_to_click = 1;
static const int tap_and_drag = 1;
static const int drag_lock = 1;
static const int natural_scrolling = 1; /* touchpads only */
static const int disable_while_typing = 1;
static const int left_handed = 0;
static const int middle_button_emulation = 0;
/* You can choose between:
LIBINPUT_CONFIG_SCROLL_NO_SCROLL
LIBINPUT_CONFIG_SCROLL_2FG
LIBINPUT_CONFIG_SCROLL_EDGE
LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN
*/
static const enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;

/* You can choose between:
LIBINPUT_CONFIG_CLICK_METHOD_NONE
LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS
LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER
*/
static const enum libinput_config_click_method click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;

/* You can choose between:
LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
LIBINPUT_CONFIG_SEND_EVENTS_DISABLED
LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE
*/
static const uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;

/* You can choose between:
LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT
LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE
*/
static const enum libinput_config_accel_profile accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
static const double accel_speed = 0.0;

/* You can choose between:
LIBINPUT_CONFIG_TAP_MAP_LRM -- 1/2/3 finger tap maps to left/right/middle
LIBINPUT_CONFIG_TAP_MAP_LMR -- 1/2/3 finger tap maps to left/middle/right
*/
static const enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

/* Bindings follow ~/.config/mango/config.conf (DESIGN.md 10) */
#define MODKEY WLR_MODIFIER_LOGO
#define SHIFTKEY WLR_MODIFIER_SHIFT
#define CTRLKEY  WLR_MODIFIER_CTRL
#define ALTKEY   WLR_MODIFIER_ALT

#define TAGKEYS(KEY,TAG) \
	{ MODKEY,       KEY, view, {.ui = 1 << TAG} }, \
	{ MODKEY|SHIFTKEY, KEY, tag,  {.ui = 1 << TAG} }

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }
#define NOCTALIA(args) SHCMD("noctalia msg " args)
#define BIN(cmd) SHCMD("$HOME/.local/bin/" cmd)

static const Key keys[] = {
	/* modifier            key                   function              argument */
	{ MODKEY,              XKB_KEY_g,            spawn,                BIN("game-session") },
	{ MODKEY|SHIFTKEY,     XKB_KEY_g,            spawn,                BIN("game-session --single-monitor") },
	{ MODKEY,              XKB_KEY_space,        spawn,                NOCTALIA("panel-toggle launcher") },
	{ MODKEY,              XKB_KEY_s,            spawn,                NOCTALIA("panel-toggle control-center") },
	/* TODO phase 3: Super+N, Super+\ and Super+C toggle built-in scratchpads (DESIGN.md 6.6) */
	{ MODKEY,              XKB_KEY_v,            spawn,                NOCTALIA("panel-toggle clipboard") },
	{ MODKEY,              XKB_KEY_y,            spawn,                NOCTALIA("panel-toggle wallpaper") },
	{ MODKEY,              XKB_KEY_comma,        spawn,                NOCTALIA("settings-toggle") },
	{ MODKEY,              XKB_KEY_x,            spawn,                NOCTALIA("panel-toggle session") },
	{ MODKEY|ALTKEY,       XKB_KEY_l,            spawn,                NOCTALIA("session lock") },
	{ MODKEY,              XKB_KEY_r,            spawn,                NOCTALIA("screenshot-region") },
	TAGKEYS(              XKB_KEY_1,           0),
	TAGKEYS(              XKB_KEY_2,           1),
	TAGKEYS(              XKB_KEY_3,           2),
	TAGKEYS(              XKB_KEY_4,           3),
	TAGKEYS(              XKB_KEY_5,           4),
	TAGKEYS(              XKB_KEY_6,           5),
	TAGKEYS(              XKB_KEY_7,           6),
	TAGKEYS(              XKB_KEY_8,           7),
	TAGKEYS(              XKB_KEY_9,           8),
	{ MODKEY,              XKB_KEY_i,            viewadj,              {.i = -1} },
	{ MODKEY,              XKB_KEY_u,            viewadj,              {.i = +1} },
	{ MODKEY,              XKB_KEY_Page_Up,      viewadj,              {.i = -1} },
	{ MODKEY,              XKB_KEY_Page_Down,    viewadj,              {.i = +1} },
	{ MODKEY|CTRLKEY,      XKB_KEY_i,            tagadj,               {.i = -1} },
	{ MODKEY|CTRLKEY,      XKB_KEY_u,            tagadj,               {.i = +1} },
	{ MODKEY|CTRLKEY,      XKB_KEY_Up,           tagadj,               {.i = -1} },
	{ MODKEY|CTRLKEY,      XKB_KEY_Down,         tagadj,               {.i = +1} },
	{ MODKEY|CTRLKEY,      XKB_KEY_k,            tagadj,               {.i = -1} },
	{ MODKEY|CTRLKEY,      XKB_KEY_j,            tagadj,               {.i = +1} },
	{ MODKEY,              XKB_KEY_h,            focusdir,             {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY,              XKB_KEY_l,            focusdir,             {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY,              XKB_KEY_j,            focusdir,             {.i = WLR_DIRECTION_DOWN} },
	{ MODKEY,              XKB_KEY_k,            focusdir,             {.i = WLR_DIRECTION_UP} },
	{ MODKEY,              XKB_KEY_Left,         focusdir,             {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY,              XKB_KEY_Right,        focusdir,             {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY,              XKB_KEY_Down,         focusdir,             {.i = WLR_DIRECTION_DOWN} },
	{ MODKEY,              XKB_KEY_Up,           focusdir,             {.i = WLR_DIRECTION_UP} },
	{ MODKEY|SHIFTKEY,     XKB_KEY_h,            swapdir,              {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|SHIFTKEY,     XKB_KEY_l,            swapdir,              {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|SHIFTKEY,     XKB_KEY_j,            swapdir,              {.i = WLR_DIRECTION_DOWN} },
	{ MODKEY|SHIFTKEY,     XKB_KEY_k,            swapdir,              {.i = WLR_DIRECTION_UP} },
	{ MODKEY|SHIFTKEY,     XKB_KEY_Left,         swapdir,              {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|SHIFTKEY,     XKB_KEY_Right,        swapdir,              {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|SHIFTKEY,     XKB_KEY_Down,         swapdir,              {.i = WLR_DIRECTION_DOWN} },
	{ MODKEY|SHIFTKEY,     XKB_KEY_Up,           swapdir,              {.i = WLR_DIRECTION_UP} },
	{ MODKEY,              XKB_KEY_bracketleft,  stackinto,            {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY,              XKB_KEY_period,       stackinto,            {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY,             XKB_KEY_bracketright,spawn,                BIN("niri-record-window") },
	{ MODKEY,              XKB_KEY_f,            cyclewidth,           {0} },
	{ MODKEY|SHIFTKEY,     XKB_KEY_f,            togglefullscreen,     {0} },
	{ MODKEY|CTRLKEY,      XKB_KEY_f,            togglemaximizescreen, {0} },
	{ MODKEY,              XKB_KEY_equal,        resizewin,            {.v = (const int[]){ +256, 0 }} },
	{ MODKEY,              XKB_KEY_minus,        resizewin,            {.v = (const int[]){ -256, 0 }} },
	{ MODKEY|SHIFTKEY,     XKB_KEY_equal,        resizewin,            {.v = (const int[]){ 0, +144 }} },
	{ MODKEY|SHIFTKEY,     XKB_KEY_minus,        resizewin,            {.v = (const int[]){ 0, -144 }} },
	{ MODKEY|SHIFTKEY,     XKB_KEY_r,            spawn,                BIN("mango-screenshot save") },
	{ MODKEY|SHIFTKEY,     XKB_KEY_t,            togglefloating,       {0} },
	{ MODKEY,              XKB_KEY_apostrophe,   cyclelayout,          {0} },
	{ MODKEY,              XKB_KEY_q,            killclient,           {0} },
	{ MODKEY|CTRLKEY,      XKB_KEY_h,            focusmon,             {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|CTRLKEY,      XKB_KEY_l,            focusmon,             {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|CTRLKEY,      XKB_KEY_Left,         focusmon,             {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|CTRLKEY,      XKB_KEY_Right,        focusmon,             {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|SHIFTKEY|CTRLKEY, XKB_KEY_h,            tagmon,               {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|SHIFTKEY|CTRLKEY, XKB_KEY_l,            tagmon,               {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|SHIFTKEY|CTRLKEY, XKB_KEY_j,            tagmon,               {.i = WLR_DIRECTION_DOWN} },
	{ MODKEY|SHIFTKEY|CTRLKEY, XKB_KEY_k,            tagmon,               {.i = WLR_DIRECTION_UP} },
	{ MODKEY|SHIFTKEY|CTRLKEY, XKB_KEY_Left,         tagmon,               {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|SHIFTKEY|CTRLKEY, XKB_KEY_Right,        tagmon,               {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|SHIFTKEY|CTRLKEY, XKB_KEY_Up,           tagmon,               {.i = WLR_DIRECTION_UP} },
	{ MODKEY|SHIFTKEY|CTRLKEY, XKB_KEY_Down,         tagmon,               {.i = WLR_DIRECTION_DOWN} },
	{ MODKEY|SHIFTKEY,     XKB_KEY_p,            spawn,                NOCTALIA("dpms-off") },
	{ 0,                   XKB_KEY_Print,        spawn,                NOCTALIA("screenshot-region") },
	{ CTRLKEY,             XKB_KEY_Print,        spawn,                BIN("mango-screenshot output") },
	{ ALTKEY,              XKB_KEY_Print,        spawn,                BIN("mango-screenshot window") },
	{ 0,                   XKB_KEY_XF86Launch1,  spawn,                NOCTALIA("screenshot-region") },
	{ CTRLKEY,             XKB_KEY_XF86Launch1,  spawn,                BIN("mango-screenshot output") },
	{ ALTKEY,              XKB_KEY_XF86Launch1,  spawn,                BIN("mango-screenshot window") },
	{ MODKEY,              XKB_KEY_t,            spawn,                SHCMD("alacritty") },
	{ MODKEY,              XKB_KEY_b,            spawn,                SHCMD("helium-browser") },
	{ MODKEY,              XKB_KEY_d,            spawn,                SHCMD("equibop") },
	{ MODKEY,              XKB_KEY_e,            spawn,                SHCMD("kumo-files") },
	/* TODO phase 5: Super+Tab overview, Alt+Tab / Alt+Shift+Tab switcher */
	{ MODKEY|SHIFTKEY,     XKB_KEY_e,            quit,                 {0} },

	/* Ctrl-Alt-Backspace and Ctrl-Alt-Fx used to be handled by X server */
	{ CTRLKEY|ALTKEY,      XKB_KEY_BackSpace,    quit,                 {0} },
	/* Ctrl-Alt-Fx is used to switch to another VT, if you don't know what a VT is
	 * do not remove them.
	 */
#define CHVT(n) { CTRLKEY|ALTKEY, XKB_KEY_F##n, chvt, {.ui = (n)} }
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};

/* Also work while the screen is locked (mango's bindl) */
static const Key lockedkeys[] = {
	{ 0, XKB_KEY_XF86AudioRaiseVolume,  spawn, NOCTALIA("volume-up 3") },
	{ 0, XKB_KEY_XF86AudioLowerVolume,  spawn, NOCTALIA("volume-down 3") },
	{ 0, XKB_KEY_XF86AudioMute,         spawn, NOCTALIA("volume-mute") },
	{ 0, XKB_KEY_XF86AudioMicMute,      spawn, NOCTALIA("mic-mute") },
	{ 0, XKB_KEY_XF86AudioNext,         spawn, NOCTALIA("media next") },
	{ 0, XKB_KEY_XF86AudioPrev,         spawn, NOCTALIA("media previous") },
	{ 0, XKB_KEY_XF86AudioPlay,         spawn, NOCTALIA("media toggle") },
	{ 0, XKB_KEY_XF86MonBrightnessUp,   spawn, NOCTALIA("brightness-up") },
	{ 0, XKB_KEY_XF86MonBrightnessDown, spawn, NOCTALIA("brightness-down") },
};

static const Button buttons[] = {
	{ MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove} },
	{ MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize} },
};

static const unsigned int axis_timeout = 100; /* ms between wheel bind actions */
static const Axis axes[] = {
	{ MODKEY|CTRLKEY,       AxisUp,    tagadj,   {.i = -1} },
	{ MODKEY|CTRLKEY,       AxisDown,  tagadj,   {.i = +1} },
	{ MODKEY|SHIFTKEY,      AxisUp,    viewadj,  {.i = -1} },
	{ MODKEY|SHIFTKEY,      AxisDown,  viewadj,  {.i = +1} },
	{ MODKEY,            AxisLeft,  focusdir, {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY,            AxisRight, focusdir, {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY,            AxisUp,    focusdir, {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY,            AxisDown,  focusdir, {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|CTRLKEY,       AxisLeft,  swapdir,  {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|CTRLKEY,       AxisRight, swapdir,  {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|CTRLKEY|SHIFTKEY, AxisUp,    swapdir,  {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|CTRLKEY|SHIFTKEY, AxisDown,  swapdir,  {.i = WLR_DIRECTION_RIGHT} },
};
