
/* Generated data (by glib-mkenums) */

#include "terminal-type-builtins.h"
#include "terminal-profile.h"
#
/* enumerations from "../../src/terminal-profile.h" */
GType
terminal_title_mode_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
 
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GEnumValue values[] = {
      { TERMINAL_TITLE_REPLACE, "TERMINAL_TITLE_REPLACE", "replace" },
      { TERMINAL_TITLE_BEFORE, "TERMINAL_TITLE_BEFORE", "before" },
      { TERMINAL_TITLE_AFTER, "TERMINAL_TITLE_AFTER", "after" },
      { TERMINAL_TITLE_IGNORE, "TERMINAL_TITLE_IGNORE", "ignore" },
      { 0, NULL, NULL }
    };
    GType g_define_type_id = \
       g_enum_register_static (/* g_intern_static_string */ ("TerminalTitleMode"), values);
      
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
    
  return g_define_type_id__volatile;
}

GType
terminal_scrollbar_position_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
 
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GEnumValue values[] = {
      { TERMINAL_SCROLLBAR_LEFT, "TERMINAL_SCROLLBAR_LEFT", "left" },
      { TERMINAL_SCROLLBAR_RIGHT, "TERMINAL_SCROLLBAR_RIGHT", "right" },
      { TERMINAL_SCROLLBAR_HIDDEN, "TERMINAL_SCROLLBAR_HIDDEN", "hidden" },
      { 0, NULL, NULL }
    };
    GType g_define_type_id = \
       g_enum_register_static (/* g_intern_static_string */ ("TerminalScrollbarPosition"), values);
      
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
    
  return g_define_type_id__volatile;
}

GType
terminal_exit_action_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
 
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GEnumValue values[] = {
      { TERMINAL_EXIT_CLOSE, "TERMINAL_EXIT_CLOSE", "close" },
      { TERMINAL_EXIT_RESTART, "TERMINAL_EXIT_RESTART", "restart" },
      { TERMINAL_EXIT_HOLD, "TERMINAL_EXIT_HOLD", "hold" },
      { 0, NULL, NULL }
    };
    GType g_define_type_id = \
       g_enum_register_static (/* g_intern_static_string */ ("TerminalExitAction"), values);
      
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
    
  return g_define_type_id__volatile;
}

GType
terminal_background_type_get_type (void)
{
  static volatile gsize g_define_type_id__volatile = 0;
 
  if (g_once_init_enter (&g_define_type_id__volatile)) {
    static const GEnumValue values[] = {
      { TERMINAL_BACKGROUND_SOLID, "TERMINAL_BACKGROUND_SOLID", "solid" },
      { TERMINAL_BACKGROUND_IMAGE, "TERMINAL_BACKGROUND_IMAGE", "image" },
      { TERMINAL_BACKGROUND_TRANSPARENT, "TERMINAL_BACKGROUND_TRANSPARENT", "transparent" },
      { 0, NULL, NULL }
    };
    GType g_define_type_id = \
       g_enum_register_static (/* g_intern_static_string */ ("TerminalBackgroundType"), values);
      
    g_once_init_leave (&g_define_type_id__volatile, g_define_type_id);
  }
    
  return g_define_type_id__volatile;
}



/* Generated data ends here */

