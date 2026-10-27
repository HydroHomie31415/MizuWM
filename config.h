/* Taken from https://github.com/djpohly/dwl/issues/466 */
#define COLOR(hex)    { ((hex >> 24) & 0xFF) / 255.0f, \
                        ((hex >> 16) & 0xFF) / 255.0f, \
                        ((hex >> 8) & 0xFF) / 255.0f, \
                        (hex & 0xFF) / 255.0f }
/* appearance */
static const int sloppyfocus               = 1;  /* focus follows mouse */
static const int warpcursor                = 1;  /* keyboard focus changes move the cursor */
static const int focusonactivate           = 1;  /* xdg-activation requests take focus, except from fullscreen */
static const int bypass_surface_visibility = 0;  /* 1 means idle inhibitors will disable idle tracking even if it's surface isn't visible  */
static const unsigned int borderpx         = 0;  /* border pixel of windows */
static const unsigned int snap             = 32; /* snap pixel */
/* colours from mango's Noctalia theme (~/.config/mango/colors.noctalia.conf) */
static const float rootcolor[]             = COLOR(0x0b0e14ff);
static const float bordercolor[]           = COLOR(0x595f6aff);
static const float focuscolor[]            = COLOR(0xe6b450ff);
static const float urgentcolor[]           = COLOR(0xd95757ff);
static const float dropcolor[]             = COLOR(0x735a2880); /* mango's drop_color 0xe6b45080, premultiplied */
/* This conforms to the xdg-protocol. Set the alpha to zero to restore the old behavior */
static const float fullscreen_bg[]         = {0.0f, 0.0f, 0.0f, 1.0f}; /* You can also use glsl colors */
/* animations in ms, mango's durations; never on game windows */
static const int animmove                  = 250;
static const int animtag                   = 250; /* slides vertically */
static const int animopen                  = 150; /* zoom and fade */
static const int animclose                 = 150;
static const float animzoomin              = 0.3f; /* opens from this size */
static const float animzoomout             = 0.8f; /* closes to this size */

/* tagging - TAGCOUNT must be no greater than 31 */
#define TAGCOUNT (9)

/* logging */
static int log_level = WLR_ERROR;

/* session (DESIGN.md 5): environment for everything mywm spawns */
static const char *const sessionenv[][2] = {
	{ "MESA_SHADER_CACHE_MAX_SIZE",  "10G" },
	{ "MESA_DISK_CACHE_SINGLE_FILE", "true" },
	{ "XDG_CURRENT_DESKTOP",         "mywm" },
	{ "XDG_SESSION_TYPE",            "wayland" },
};
/* Started on real hardware only, never nested or headless. The first hands
 * the session environment to D-Bus activated services such as portals, then
 * starts mywm-session.target, which brings up graphical-session.target and
 * with it the shell (dms.service). Without that unit the shell runs directly. */
static const char *const autostart[] = {
	"dbus-update-activation-environment --systemd WAYLAND_DISPLAY DISPLAY "
		"XDG_CURRENT_DESKTOP XDG_SESSION_TYPE MYWM_SOCKET "
		"MANGO_INSTANCE_SIGNATURE; "
		"systemctl --user start mywm-session.target || exec dms run",
	"gnome-keyring-daemon --start --components=secrets",
	"vesktop",
	"steam",
	"helium",
};

/* Regexes (POSIX extended) on app_id and title, from mango's window rules */
#ifndef GAMEMON
#define GAMEMON "DP-3" /* the gaming monitor (DESIGN.md 5) */
#endif
#ifndef SIDEMON
#define SIDEMON "DP-1" /* the second monitor */
#endif
#define GAMEIDS "^(steam_app_[0-9]+|valheim\\.x86_64|cs2|gamescope)$"
static const Rule rules[] = {
	{ .id = "^(org\\.gnome\\.Calculator|gnome-calculator|galculator|blueman-manager|org\\.gnome\\.Nautilus|xdg-desktop-portal.*|zoom)$",
	  .isfloating = 1 },
	{ .id = "^(firefox|zen)$", .title = "^Picture-in-Picture$", .isfloating = 1 },
	{ .id = "^(dev\\.duxlabs\\.shell|com\\.danklinux\\.dms)$", .isfloating = 1 },
	{ .id = "^steam$", .title = "^notificationtoasts_[0-9]+_desktop$",
	  .isfloating = 1, .nofocus = 1, .offx = 100, .offy = 100 },
	{ .id = "^steam$", .nomapfs = 1, .monitor = SIDEMON },
	{ .id = "^[Vv]esktop$", .monitor = SIDEMON },
	{ .id = "^([Hh]elium|helium-browser)$", .monitor = GAMEMON },
	/* Games (DESIGN.md 6.8). Proton maps an untitled splash first, and
	 * fullscreening that breaks mouse capture, so wait for a title. */
	{ .id = GAMEIDS, .isgame = 1, .monitor = GAMEMON, .nomapfs = 1 },
	{ .id = GAMEIDS, .title = ".", .isfullscreen = 1 },
	/* "Minecraft* 1.21.1 - Singleplayer", not a browser tab about it */
	{ .title = "^Minecraft\\*? [0-9.]+( - .*)?$", .isgame = 1, .monitor = GAMEMON, .isfullscreen = 1 },
};

/* Named scratchpads (DESIGN.md 6.6): floating overlay windows, toggled by
 * key or by mywmctl dispatch toggle_scratchpad <name> */
static const int scratchpad_width = 1792, scratchpad_height = 1152;
static const Scratchpad scratchpads[] = {
	/* name      app_id regex                  title regex            command */
	{ "notes",   "^md\\.obsidian\\.Obsidian$", NULL,                  "obsidian" },
	{ "term",    "^scratch-term$",             NULL,                  "alacritty --class scratch-term" },
	{ "vscode",  "^com\\.microsoft\\.VSCode$", "VS Code Scratchpad$",
		"code --user-data-dir ~/.local/share/code-scratchpad --new-window" },
	{ "spotify", "^spotify$",                  NULL,
		"spotify --enable-features=UseOzonePlatform --ozone-platform=wayland" },
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
   /* name        mfact  nmaster scale layout       rotate/reflect                x     y   w     h     refresh (mHz)
    * example of a HiDPI laptop monitor:
    { "eDP-1",    0.5f,  1,      2,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,   -1, 0,    0,    0 }, */
	/* DESIGN.md 5, from mango's monitor_rule lines */
	{ "DP-1",     0.55f, 1,      1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   0,    0,  2560, 1440, 143973 },
	{ "DP-3",     0.55f, 1,      1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   2560, 0,  2560, 1440, 239992 },
	{ NULL,       0.55f, 1,      1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,   -1, 0,    0,    0 },
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
#define DMS(args) SHCMD("dms ipc call " args)
#define BIN(cmd) SHCMD("$HOME/.local/bin/" cmd)

static const Key keys[] = {
	/* modifier            key                   function              argument */
	{ MODKEY,              XKB_KEY_g,            spawn,                BIN("game-session") },
	{ MODKEY|SHIFTKEY,     XKB_KEY_g,            spawn,                BIN("game-session --single-monitor") },
	{ MODKEY,              XKB_KEY_space,        spawn,                DMS("spotlight toggle") },
	{ MODKEY,              XKB_KEY_s,            togglescratch,        {.v = &scratchpads[3]} },
	{ MODKEY|SHIFTKEY,     XKB_KEY_s,            spawn,                DMS("control-center toggle") },
	{ MODKEY,              XKB_KEY_n,            togglescratch,        {.v = &scratchpads[0]} },
	{ MODKEY,              XKB_KEY_backslash,    togglescratch,        {.v = &scratchpads[1]} },
	{ MODKEY,              XKB_KEY_c,            togglescratch,        {.v = &scratchpads[2]} },
	{ MODKEY,              XKB_KEY_v,            spawn,                DMS("clipboard toggle") },
	{ MODKEY,              XKB_KEY_y,            spawn,                DMS("dankdash wallpaper") },
	{ MODKEY,              XKB_KEY_comma,        spawn,                DMS("settings focusOrToggle") },
	{ MODKEY,              XKB_KEY_x,            spawn,                DMS("powermenu toggle") },
	{ MODKEY|ALTKEY,       XKB_KEY_l,            spawn,                DMS("lock lock") },
	{ MODKEY,              XKB_KEY_r,            spawn,                SHCMD("dms screenshot region") },
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
	{ MODKEY|SHIFTKEY,     XKB_KEY_r,            spawn,                SHCMD("dms screenshot full") },
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
	{ MODKEY|SHIFTKEY,     XKB_KEY_p,            spawn,                SHCMD("dms dpms off") },
	{ 0,                   XKB_KEY_Print,        spawn,                SHCMD("dms screenshot region") },
	{ CTRLKEY,             XKB_KEY_Print,        spawn,                SHCMD("dms screenshot full") },
	{ ALTKEY,              XKB_KEY_Print,        spawn,                SHCMD("dms screenshot window") },
	{ 0,                   XKB_KEY_XF86Launch1,  spawn,                SHCMD("dms screenshot region") },
	{ CTRLKEY,             XKB_KEY_XF86Launch1,  spawn,                SHCMD("dms screenshot full") },
	{ ALTKEY,              XKB_KEY_XF86Launch1,  spawn,                SHCMD("dms screenshot window") },
	{ MODKEY,              XKB_KEY_t,            spawn,                SHCMD("alacritty") },
	{ MODKEY,              XKB_KEY_b,            spawn,                SHCMD("helium") },
	{ MODKEY,              XKB_KEY_d,            spawn,                SHCMD("equibop") },
	{ MODKEY,              XKB_KEY_e,            spawn,                SHCMD("dfiles open --new-window") },
	{ MODKEY,              XKB_KEY_Tab,          overview,             {0} },
	{ ALTKEY,              XKB_KEY_Tab,          switcher,             {.i = +1} },
	{ ALTKEY|SHIFTKEY,     XKB_KEY_Tab,          switcher,             {.i = -1} },
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
	{ 0, XKB_KEY_XF86AudioRaiseVolume,  spawn, DMS("audio increment 3") },
	{ 0, XKB_KEY_XF86AudioLowerVolume,  spawn, DMS("audio decrement 3") },
	{ 0, XKB_KEY_XF86AudioMute,         spawn, DMS("audio mute") },
	{ 0, XKB_KEY_XF86AudioMicMute,      spawn, DMS("audio micmute") },
	{ 0, XKB_KEY_XF86AudioNext,         spawn, DMS("mpris next") },
	{ 0, XKB_KEY_XF86AudioPrev,         spawn, DMS("mpris previous") },
	{ 0, XKB_KEY_XF86AudioPlay,         spawn, DMS("mpris playPause") },
	{ 0, XKB_KEY_XF86MonBrightnessUp,   spawn, DMS("brightness increment 5 \"\"") },
	{ 0, XKB_KEY_XF86MonBrightnessDown, spawn, DMS("brightness decrement 5 \"\"") },
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
