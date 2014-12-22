#ifndef loliwm_config
#define loliwm_config

#define EXIT_KEY XKB_KEY_Escape
#define CLOSE_FOCUS_KEY XKB_KEY_q
#define TERM_OPEN_KEY XKB_KEY_Return
#define MENU_OPEN_KEY XKB_KEY_p
#define TOGGLE_FULLSCREEN_KEY XKB_KEY_f
#define CYCLE_CLIENT_KEY XKB_KEY_h
#define NMASTER_SHRINK_KEY XKB_KEY_o
#define NMASTER_EXPAND_KEY XKB_KEY_i
#define MOVE_FOCUS_OUTPUT_ONE XKB_KEY_z
#define MOVE_FOCUS_OUTPUT_TWO XKB_KEY_x
#define MOVE_FOCUS_OUTPUT_THREE XKB_KEY_c
#define ROTATE_OUTPUT_FOCUS_KEY XKB_KEY_l
#define MOVE_CLIENT_FOCUS_LEFT XKB_KEY_j
#define MOVE_CLIENT_FOCUS_RIGHT XKB_KEY_k
#define SCREENSHOT_KEY XKB_KEY_SunPrint_Screen //This is what i mean by this.
/* 
  This does not work with --prefix alt, because on linux you use sysrq by
  doing alt+printscreen+[insert sysrq command key here], and therfore
  if you hit alt and print screen together it is caught by the kernel.
  You might be able to remady this by running:
  echo "0" > /proc/sys/kernel/sysrq which disables sysrq (it is disabled
  by default on some distros.
  (ignore this if running bsd, mach, etc...)
*/

#define DEFAULT_TERM "weston-terminal"
#define MENU_APP "bemenu-run"
#endif
