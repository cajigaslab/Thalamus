#pragma once

#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

typedef enum THALAMUS_SDL_Scancode
{
    THALAMUS_SDL_SCANCODE_UNKNOWN = 0,

    /**
     *  \name Usage page 0x07
     *
     *  These values are from usage page 0x07 (USB keyboard page).
     */
    /* @{ */

    THALAMUS_SDL_SCANCODE_A = 4,
    THALAMUS_SDL_SCANCODE_B = 5,
    THALAMUS_SDL_SCANCODE_C = 6,
    THALAMUS_SDL_SCANCODE_D = 7,
    THALAMUS_SDL_SCANCODE_E = 8,
    THALAMUS_SDL_SCANCODE_F = 9,
    THALAMUS_SDL_SCANCODE_G = 10,
    THALAMUS_SDL_SCANCODE_H = 11,
    THALAMUS_SDL_SCANCODE_I = 12,
    THALAMUS_SDL_SCANCODE_J = 13,
    THALAMUS_SDL_SCANCODE_K = 14,
    THALAMUS_SDL_SCANCODE_L = 15,
    THALAMUS_SDL_SCANCODE_M = 16,
    THALAMUS_SDL_SCANCODE_N = 17,
    THALAMUS_SDL_SCANCODE_O = 18,
    THALAMUS_SDL_SCANCODE_P = 19,
    THALAMUS_SDL_SCANCODE_Q = 20,
    THALAMUS_SDL_SCANCODE_R = 21,
    THALAMUS_SDL_SCANCODE_S = 22,
    THALAMUS_SDL_SCANCODE_T = 23,
    THALAMUS_SDL_SCANCODE_U = 24,
    THALAMUS_SDL_SCANCODE_V = 25,
    THALAMUS_SDL_SCANCODE_W = 26,
    THALAMUS_SDL_SCANCODE_X = 27,
    THALAMUS_SDL_SCANCODE_Y = 28,
    THALAMUS_SDL_SCANCODE_Z = 29,

    THALAMUS_SDL_SCANCODE_1 = 30,
    THALAMUS_SDL_SCANCODE_2 = 31,
    THALAMUS_SDL_SCANCODE_3 = 32,
    THALAMUS_SDL_SCANCODE_4 = 33,
    THALAMUS_SDL_SCANCODE_5 = 34,
    THALAMUS_SDL_SCANCODE_6 = 35,
    THALAMUS_SDL_SCANCODE_7 = 36,
    THALAMUS_SDL_SCANCODE_8 = 37,
    THALAMUS_SDL_SCANCODE_9 = 38,
    THALAMUS_SDL_SCANCODE_0 = 39,

    THALAMUS_SDL_SCANCODE_RETURN = 40,
    THALAMUS_SDL_SCANCODE_ESCAPE = 41,
    THALAMUS_SDL_SCANCODE_BACKSPACE = 42,
    THALAMUS_SDL_SCANCODE_TAB = 43,
    THALAMUS_SDL_SCANCODE_SPACE = 44,

    THALAMUS_SDL_SCANCODE_MINUS = 45,
    THALAMUS_SDL_SCANCODE_EQUALS = 46,
    THALAMUS_SDL_SCANCODE_LEFTBRACKET = 47,
    THALAMUS_SDL_SCANCODE_RIGHTBRACKET = 48,
    THALAMUS_SDL_SCANCODE_BACKSLASH = 49, /**< Located at the lower left of the return
                                  *   key on ISO keyboards and at the right end
                                  *   of the QWERTY row on ANSI keyboards.
                                  *   Produces REVERSE SOLIDUS (backslash) and
                                  *   VERTICAL LINE in a US layout, REVERSE
                                  *   SOLIDUS and VERTICAL LINE in a UK Mac
                                  *   layout, NUMBER SIGN and TILDE in a UK
                                  *   Windows layout, DOLLAR SIGN and POUND SIGN
                                  *   in a Swiss German layout, NUMBER SIGN and
                                  *   APOSTROPHE in a German layout, GRAVE
                                  *   ACCENT and POUND SIGN in a French Mac
                                  *   layout, and ASTERISK and MICRO SIGN in a
                                  *   French Windows layout.
                                  */
    THALAMUS_SDL_SCANCODE_NONUSHASH = 50, /**< ISO USB keyboards actually use this code
                                  *   instead of 49 for the same key, but all
                                  *   OSes I've seen treat the two codes
                                  *   identically. So, as an implementer, unless
                                  *   your keyboard generates both of those
                                  *   codes and your OS treats them differently,
                                  *   you should generate THALAMUS_SDL_SCANCODE_BACKSLASH
                                  *   instead of this code. As a user, you
                                  *   should not rely on this code because SDL
                                  *   will never generate it with most (all?)
                                  *   keyboards.
                                  */
    THALAMUS_SDL_SCANCODE_SEMICOLON = 51,
    THALAMUS_SDL_SCANCODE_APOSTROPHE = 52,
    THALAMUS_SDL_SCANCODE_GRAVE = 53, /**< Located in the top left corner (on both ANSI
                              *   and ISO keyboards). Produces GRAVE ACCENT and
                              *   TILDE in a US Windows layout and in US and UK
                              *   Mac layouts on ANSI keyboards, GRAVE ACCENT
                              *   and NOT SIGN in a UK Windows layout, SECTION
                              *   SIGN and PLUS-MINUS SIGN in US and UK Mac
                              *   layouts on ISO keyboards, SECTION SIGN and
                              *   DEGREE SIGN in a Swiss German layout (Mac:
                              *   only on ISO keyboards), CIRCUMFLEX ACCENT and
                              *   DEGREE SIGN in a German layout (Mac: only on
                              *   ISO keyboards), SUPERSCRIPT TWO and TILDE in a
                              *   French Windows layout, COMMERCIAL AT and
                              *   NUMBER SIGN in a French Mac layout on ISO
                              *   keyboards, and LESS-THAN SIGN and GREATER-THAN
                              *   SIGN in a Swiss German, German, or French Mac
                              *   layout on ANSI keyboards.
                              */
    THALAMUS_SDL_SCANCODE_COMMA = 54,
    THALAMUS_SDL_SCANCODE_PERIOD = 55,
    THALAMUS_SDL_SCANCODE_SLASH = 56,

    THALAMUS_SDL_SCANCODE_CAPSLOCK = 57,

    THALAMUS_SDL_SCANCODE_F1 = 58,
    THALAMUS_SDL_SCANCODE_F2 = 59,
    THALAMUS_SDL_SCANCODE_F3 = 60,
    THALAMUS_SDL_SCANCODE_F4 = 61,
    THALAMUS_SDL_SCANCODE_F5 = 62,
    THALAMUS_SDL_SCANCODE_F6 = 63,
    THALAMUS_SDL_SCANCODE_F7 = 64,
    THALAMUS_SDL_SCANCODE_F8 = 65,
    THALAMUS_SDL_SCANCODE_F9 = 66,
    THALAMUS_SDL_SCANCODE_F10 = 67,
    THALAMUS_SDL_SCANCODE_F11 = 68,
    THALAMUS_SDL_SCANCODE_F12 = 69,

    THALAMUS_SDL_SCANCODE_PRINTSCREEN = 70,
    THALAMUS_SDL_SCANCODE_SCROLLLOCK = 71,
    THALAMUS_SDL_SCANCODE_PAUSE = 72,
    THALAMUS_SDL_SCANCODE_INSERT = 73, /**< insert on PC, help on some Mac keyboards (but
                                   does send code 73, not 117) */
    THALAMUS_SDL_SCANCODE_HOME = 74,
    THALAMUS_SDL_SCANCODE_PAGEUP = 75,
    THALAMUS_SDL_SCANCODE_DELETE = 76,
    THALAMUS_SDL_SCANCODE_END = 77,
    THALAMUS_SDL_SCANCODE_PAGEDOWN = 78,
    THALAMUS_SDL_SCANCODE_RIGHT = 79,
    THALAMUS_SDL_SCANCODE_LEFT = 80,
    THALAMUS_SDL_SCANCODE_DOWN = 81,
    THALAMUS_SDL_SCANCODE_UP = 82,

    THALAMUS_SDL_SCANCODE_NUMLOCKCLEAR = 83, /**< num lock on PC, clear on Mac keyboards
                                     */
    THALAMUS_SDL_SCANCODE_KP_DIVIDE = 84,
    THALAMUS_SDL_SCANCODE_KP_MULTIPLY = 85,
    THALAMUS_SDL_SCANCODE_KP_MINUS = 86,
    THALAMUS_SDL_SCANCODE_KP_PLUS = 87,
    THALAMUS_SDL_SCANCODE_KP_ENTER = 88,
    THALAMUS_SDL_SCANCODE_KP_1 = 89,
    THALAMUS_SDL_SCANCODE_KP_2 = 90,
    THALAMUS_SDL_SCANCODE_KP_3 = 91,
    THALAMUS_SDL_SCANCODE_KP_4 = 92,
    THALAMUS_SDL_SCANCODE_KP_5 = 93,
    THALAMUS_SDL_SCANCODE_KP_6 = 94,
    THALAMUS_SDL_SCANCODE_KP_7 = 95,
    THALAMUS_SDL_SCANCODE_KP_8 = 96,
    THALAMUS_SDL_SCANCODE_KP_9 = 97,
    THALAMUS_SDL_SCANCODE_KP_0 = 98,
    THALAMUS_SDL_SCANCODE_KP_PERIOD = 99,

    THALAMUS_SDL_SCANCODE_NONUSBACKSLASH = 100, /**< This is the additional key that ISO
                                        *   keyboards have over ANSI ones,
                                        *   located between left shift and Z.
                                        *   Produces GRAVE ACCENT and TILDE in a
                                        *   US or UK Mac layout, REVERSE SOLIDUS
                                        *   (backslash) and VERTICAL LINE in a
                                        *   US or UK Windows layout, and
                                        *   LESS-THAN SIGN and GREATER-THAN SIGN
                                        *   in a Swiss German, German, or French
                                        *   layout. */
    THALAMUS_SDL_SCANCODE_APPLICATION = 101, /**< windows contextual menu, compose */
    THALAMUS_SDL_SCANCODE_POWER = 102, /**< The USB document says this is a status flag,
                               *   not a physical key - but some Mac keyboards
                               *   do have a power key. */
    THALAMUS_SDL_SCANCODE_KP_EQUALS = 103,
    THALAMUS_SDL_SCANCODE_F13 = 104,
    THALAMUS_SDL_SCANCODE_F14 = 105,
    THALAMUS_SDL_SCANCODE_F15 = 106,
    THALAMUS_SDL_SCANCODE_F16 = 107,
    THALAMUS_SDL_SCANCODE_F17 = 108,
    THALAMUS_SDL_SCANCODE_F18 = 109,
    THALAMUS_SDL_SCANCODE_F19 = 110,
    THALAMUS_SDL_SCANCODE_F20 = 111,
    THALAMUS_SDL_SCANCODE_F21 = 112,
    THALAMUS_SDL_SCANCODE_F22 = 113,
    THALAMUS_SDL_SCANCODE_F23 = 114,
    THALAMUS_SDL_SCANCODE_F24 = 115,
    THALAMUS_SDL_SCANCODE_EXECUTE = 116,
    THALAMUS_SDL_SCANCODE_HELP = 117,    /**< AL Integrated Help Center */
    THALAMUS_SDL_SCANCODE_MENU = 118,    /**< Menu (show menu) */
    THALAMUS_SDL_SCANCODE_SELECT = 119,
    THALAMUS_SDL_SCANCODE_STOP = 120,    /**< AC Stop */
    THALAMUS_SDL_SCANCODE_AGAIN = 121,   /**< AC Redo/Repeat */
    THALAMUS_SDL_SCANCODE_UNDO = 122,    /**< AC Undo */
    THALAMUS_SDL_SCANCODE_CUT = 123,     /**< AC Cut */
    THALAMUS_SDL_SCANCODE_COPY = 124,    /**< AC Copy */
    THALAMUS_SDL_SCANCODE_PASTE = 125,   /**< AC Paste */
    THALAMUS_SDL_SCANCODE_FIND = 126,    /**< AC Find */
    THALAMUS_SDL_SCANCODE_MUTE = 127,
    THALAMUS_SDL_SCANCODE_VOLUMEUP = 128,
    THALAMUS_SDL_SCANCODE_VOLUMEDOWN = 129,
/* not sure whether there's a reason to enable these */
/*     THALAMUS_SDL_SCANCODE_LOCKINGCAPSLOCK = 130,  */
/*     THALAMUS_SDL_SCANCODE_LOCKINGNUMLOCK = 131, */
/*     THALAMUS_SDL_SCANCODE_LOCKINGSCROLLLOCK = 132, */
    THALAMUS_SDL_SCANCODE_KP_COMMA = 133,
    THALAMUS_SDL_SCANCODE_KP_EQUALSAS400 = 134,

    THALAMUS_SDL_SCANCODE_INTERNATIONAL1 = 135, /**< used on Asian keyboards, see
                                            footnotes in USB doc */
    THALAMUS_SDL_SCANCODE_INTERNATIONAL2 = 136,
    THALAMUS_SDL_SCANCODE_INTERNATIONAL3 = 137, /**< Yen */
    THALAMUS_SDL_SCANCODE_INTERNATIONAL4 = 138,
    THALAMUS_SDL_SCANCODE_INTERNATIONAL5 = 139,
    THALAMUS_SDL_SCANCODE_INTERNATIONAL6 = 140,
    THALAMUS_SDL_SCANCODE_INTERNATIONAL7 = 141,
    THALAMUS_SDL_SCANCODE_INTERNATIONAL8 = 142,
    THALAMUS_SDL_SCANCODE_INTERNATIONAL9 = 143,
    THALAMUS_SDL_SCANCODE_LANG1 = 144, /**< Hangul/English toggle */
    THALAMUS_SDL_SCANCODE_LANG2 = 145, /**< Hanja conversion */
    THALAMUS_SDL_SCANCODE_LANG3 = 146, /**< Katakana */
    THALAMUS_SDL_SCANCODE_LANG4 = 147, /**< Hiragana */
    THALAMUS_SDL_SCANCODE_LANG5 = 148, /**< Zenkaku/Hankaku */
    THALAMUS_SDL_SCANCODE_LANG6 = 149, /**< reserved */
    THALAMUS_SDL_SCANCODE_LANG7 = 150, /**< reserved */
    THALAMUS_SDL_SCANCODE_LANG8 = 151, /**< reserved */
    THALAMUS_SDL_SCANCODE_LANG9 = 152, /**< reserved */

    THALAMUS_SDL_SCANCODE_ALTERASE = 153,    /**< Erase-Eaze */
    THALAMUS_SDL_SCANCODE_SYSREQ = 154,
    THALAMUS_SDL_SCANCODE_CANCEL = 155,      /**< AC Cancel */
    THALAMUS_SDL_SCANCODE_CLEAR = 156,
    THALAMUS_SDL_SCANCODE_PRIOR = 157,
    THALAMUS_SDL_SCANCODE_RETURN2 = 158,
    THALAMUS_SDL_SCANCODE_SEPARATOR = 159,
    THALAMUS_SDL_SCANCODE_OUT = 160,
    THALAMUS_SDL_SCANCODE_OPER = 161,
    THALAMUS_SDL_SCANCODE_CLEARAGAIN = 162,
    THALAMUS_SDL_SCANCODE_CRSEL = 163,
    THALAMUS_SDL_SCANCODE_EXSEL = 164,

    THALAMUS_SDL_SCANCODE_KP_00 = 176,
    THALAMUS_SDL_SCANCODE_KP_000 = 177,
    THALAMUS_SDL_SCANCODE_THOUSANDSSEPARATOR = 178,
    THALAMUS_SDL_SCANCODE_DECIMALSEPARATOR = 179,
    THALAMUS_SDL_SCANCODE_CURRENCYUNIT = 180,
    THALAMUS_SDL_SCANCODE_CURRENCYSUBUNIT = 181,
    THALAMUS_SDL_SCANCODE_KP_LEFTPAREN = 182,
    THALAMUS_SDL_SCANCODE_KP_RIGHTPAREN = 183,
    THALAMUS_SDL_SCANCODE_KP_LEFTBRACE = 184,
    THALAMUS_SDL_SCANCODE_KP_RIGHTBRACE = 185,
    THALAMUS_SDL_SCANCODE_KP_TAB = 186,
    THALAMUS_SDL_SCANCODE_KP_BACKSPACE = 187,
    THALAMUS_SDL_SCANCODE_KP_A = 188,
    THALAMUS_SDL_SCANCODE_KP_B = 189,
    THALAMUS_SDL_SCANCODE_KP_C = 190,
    THALAMUS_SDL_SCANCODE_KP_D = 191,
    THALAMUS_SDL_SCANCODE_KP_E = 192,
    THALAMUS_SDL_SCANCODE_KP_F = 193,
    THALAMUS_SDL_SCANCODE_KP_XOR = 194,
    THALAMUS_SDL_SCANCODE_KP_POWER = 195,
    THALAMUS_SDL_SCANCODE_KP_PERCENT = 196,
    THALAMUS_SDL_SCANCODE_KP_LESS = 197,
    THALAMUS_SDL_SCANCODE_KP_GREATER = 198,
    THALAMUS_SDL_SCANCODE_KP_AMPERSAND = 199,
    THALAMUS_SDL_SCANCODE_KP_DBLAMPERSAND = 200,
    THALAMUS_SDL_SCANCODE_KP_VERTICALBAR = 201,
    THALAMUS_SDL_SCANCODE_KP_DBLVERTICALBAR = 202,
    THALAMUS_SDL_SCANCODE_KP_COLON = 203,
    THALAMUS_SDL_SCANCODE_KP_HASH = 204,
    THALAMUS_SDL_SCANCODE_KP_SPACE = 205,
    THALAMUS_SDL_SCANCODE_KP_AT = 206,
    THALAMUS_SDL_SCANCODE_KP_EXCLAM = 207,
    THALAMUS_SDL_SCANCODE_KP_MEMSTORE = 208,
    THALAMUS_SDL_SCANCODE_KP_MEMRECALL = 209,
    THALAMUS_SDL_SCANCODE_KP_MEMCLEAR = 210,
    THALAMUS_SDL_SCANCODE_KP_MEMADD = 211,
    THALAMUS_SDL_SCANCODE_KP_MEMSUBTRACT = 212,
    THALAMUS_SDL_SCANCODE_KP_MEMMULTIPLY = 213,
    THALAMUS_SDL_SCANCODE_KP_MEMDIVIDE = 214,
    THALAMUS_SDL_SCANCODE_KP_PLUSMINUS = 215,
    THALAMUS_SDL_SCANCODE_KP_CLEAR = 216,
    THALAMUS_SDL_SCANCODE_KP_CLEARENTRY = 217,
    THALAMUS_SDL_SCANCODE_KP_BINARY = 218,
    THALAMUS_SDL_SCANCODE_KP_OCTAL = 219,
    THALAMUS_SDL_SCANCODE_KP_DECIMAL = 220,
    THALAMUS_SDL_SCANCODE_KP_HEXADECIMAL = 221,

    THALAMUS_SDL_SCANCODE_LCTRL = 224,
    THALAMUS_SDL_SCANCODE_LSHIFT = 225,
    THALAMUS_SDL_SCANCODE_LALT = 226, /**< alt, option */
    THALAMUS_SDL_SCANCODE_LGUI = 227, /**< windows, command (apple), meta */
    THALAMUS_SDL_SCANCODE_RCTRL = 228,
    THALAMUS_SDL_SCANCODE_RSHIFT = 229,
    THALAMUS_SDL_SCANCODE_RALT = 230, /**< alt gr, option */
    THALAMUS_SDL_SCANCODE_RGUI = 231, /**< windows, command (apple), meta */

    THALAMUS_SDL_SCANCODE_MODE = 257,    /**< I'm not sure if this is really not covered
                                 *   by any of the above, but since there's a
                                 *   special THALAMUS_SDL_KMOD_MODE for it I'm adding it here
                                 */

    /* @} *//* Usage page 0x07 */

    /**
     *  \name Usage page 0x0C
     *
     *  These values are mapped from usage page 0x0C (USB consumer page).
     *
     *  There are way more keys in the spec than we can represent in the
     *  current scancode range, so pick the ones that commonly come up in
     *  real world usage.
     */
    /* @{ */

    THALAMUS_SDL_SCANCODE_SLEEP = 258,                   /**< Sleep */
    THALAMUS_SDL_SCANCODE_WAKE = 259,                    /**< Wake */

    THALAMUS_SDL_SCANCODE_CHANNEL_INCREMENT = 260,       /**< Channel Increment */
    THALAMUS_SDL_SCANCODE_CHANNEL_DECREMENT = 261,       /**< Channel Decrement */

    THALAMUS_SDL_SCANCODE_MEDIA_PLAY = 262,          /**< Play */
    THALAMUS_SDL_SCANCODE_MEDIA_PAUSE = 263,         /**< Pause */
    THALAMUS_SDL_SCANCODE_MEDIA_RECORD = 264,        /**< Record */
    THALAMUS_SDL_SCANCODE_MEDIA_FAST_FORWARD = 265,  /**< Fast Forward */
    THALAMUS_SDL_SCANCODE_MEDIA_REWIND = 266,        /**< Rewind */
    THALAMUS_SDL_SCANCODE_MEDIA_NEXT_TRACK = 267,    /**< Next Track */
    THALAMUS_SDL_SCANCODE_MEDIA_PREVIOUS_TRACK = 268, /**< Previous Track */
    THALAMUS_SDL_SCANCODE_MEDIA_STOP = 269,          /**< Stop */
    THALAMUS_SDL_SCANCODE_MEDIA_EJECT = 270,         /**< Eject */
    THALAMUS_SDL_SCANCODE_MEDIA_PLAY_PAUSE = 271,    /**< Play / Pause */
    THALAMUS_SDL_SCANCODE_MEDIA_SELECT = 272,        /* Media Select */

    THALAMUS_SDL_SCANCODE_AC_NEW = 273,              /**< AC New */
    THALAMUS_SDL_SCANCODE_AC_OPEN = 274,             /**< AC Open */
    THALAMUS_SDL_SCANCODE_AC_CLOSE = 275,            /**< AC Close */
    THALAMUS_SDL_SCANCODE_AC_EXIT = 276,             /**< AC Exit */
    THALAMUS_SDL_SCANCODE_AC_SAVE = 277,             /**< AC Save */
    THALAMUS_SDL_SCANCODE_AC_PRINT = 278,            /**< AC Print */
    THALAMUS_SDL_SCANCODE_AC_PROPERTIES = 279,       /**< AC Properties */

    THALAMUS_SDL_SCANCODE_AC_SEARCH = 280,           /**< AC Search */
    THALAMUS_SDL_SCANCODE_AC_HOME = 281,             /**< AC Home */
    THALAMUS_SDL_SCANCODE_AC_BACK = 282,             /**< AC Back */
    THALAMUS_SDL_SCANCODE_AC_FORWARD = 283,          /**< AC Forward */
    THALAMUS_SDL_SCANCODE_AC_STOP = 284,             /**< AC Stop */
    THALAMUS_SDL_SCANCODE_AC_REFRESH = 285,          /**< AC Refresh */
    THALAMUS_SDL_SCANCODE_AC_BOOKMARKS = 286,        /**< AC Bookmarks */

    /* @} *//* Usage page 0x0C */


    /**
     *  \name Mobile keys
     *
     *  These are values that are often used on mobile phones.
     */
    /* @{ */

    THALAMUS_SDL_SCANCODE_SOFTLEFT = 287, /**< Usually situated below the display on phones and
                                      used as a multi-function feature key for selecting
                                      a software defined function shown on the bottom left
                                      of the display. */
    THALAMUS_SDL_SCANCODE_SOFTRIGHT = 288, /**< Usually situated below the display on phones and
                                       used as a multi-function feature key for selecting
                                       a software defined function shown on the bottom right
                                       of the display. */
    THALAMUS_SDL_SCANCODE_CALL = 289, /**< Used for accepting phone calls. */
    THALAMUS_SDL_SCANCODE_ENDCALL = 290, /**< Used for rejecting phone calls. */

    /* @} *//* Mobile keys */

    /* Add any other keys here. */

    THALAMUS_SDL_SCANCODE_RESERVED = 400,    /**< 400-500 reserved for dynamic keycodes */

    THALAMUS_SDL_SCANCODE_COUNT = 512 /**< not a key, just marks the number of scancodes for array bounds */

} THALAMUS_SDL_Scancode;

typedef enum THALAMUS_SDL_MouseWheelDirection
{
    THALAMUS_SDL_MOUSEWHEEL_NORMAL,    /**< The scroll direction is normal */
    THALAMUS_SDL_MOUSEWHEEL_FLIPPED    /**< The scroll direction is flipped / natural */
} THALAMUS_SDL_MouseWheelDirection;

typedef enum THALAMUS_SDL_EventType
{
    THALAMUS_SDL_EVENT_FIRST     = 0,     /**< Unused (do not remove) */

    /* Application events */
    THALAMUS_SDL_EVENT_QUIT           = 0x100, /**< User-requested quit */

    /* These application events have special meaning on iOS and Android, see README-ios.md and README-android.md for details */
    THALAMUS_SDL_EVENT_TERMINATING,      /**< The application is being terminated by the OS. This event must be handled in a callback set with THALAMUS_SDL_AddEventWatch().
                                     Called on iOS in applicationWillTerminate()
                                     Called on Android in onDestroy()
                                */
    THALAMUS_SDL_EVENT_LOW_MEMORY,       /**< The application is low on memory, free memory if possible. This event must be handled in a callback set with THALAMUS_SDL_AddEventWatch().
                                     Called on iOS in applicationDidReceiveMemoryWarning()
                                     Called on Android in onTrimMemory()
                                */
    THALAMUS_SDL_EVENT_WILL_ENTER_BACKGROUND, /**< The application is about to enter the background. This event must be handled in a callback set with THALAMUS_SDL_AddEventWatch().
                                     Called on iOS in applicationWillResignActive()
                                     Called on Android in onPause()
                                */
    THALAMUS_SDL_EVENT_DID_ENTER_BACKGROUND, /**< The application did enter the background and may not get CPU for some time. This event must be handled in a callback set with THALAMUS_SDL_AddEventWatch().
                                     Called on iOS in applicationDidEnterBackground()
                                     Called on Android in onPause()
                                */
    THALAMUS_SDL_EVENT_WILL_ENTER_FOREGROUND, /**< The application is about to enter the foreground. This event must be handled in a callback set with THALAMUS_SDL_AddEventWatch().
                                     Called on iOS in applicationWillEnterForeground()
                                     Called on Android in onResume()
                                */
    THALAMUS_SDL_EVENT_DID_ENTER_FOREGROUND, /**< The application is now interactive. This event must be handled in a callback set with THALAMUS_SDL_AddEventWatch().
                                     Called on iOS in applicationDidBecomeActive()
                                     Called on Android in onResume()
                                */

    THALAMUS_SDL_EVENT_LOCALE_CHANGED,  /**< The user's locale preferences have changed. */

    THALAMUS_SDL_EVENT_SYSTEM_THEME_CHANGED, /**< The system theme changed */

    /* Display events */
    /* 0x150 was THALAMUS_SDL_DISPLAYEVENT, reserve the number for sdl2-compat */
    THALAMUS_SDL_EVENT_DISPLAY_ORIENTATION = 0x151,   /**< Display orientation has changed to data1 */
    THALAMUS_SDL_EVENT_DISPLAY_ADDED,                 /**< Display has been added to the system */
    THALAMUS_SDL_EVENT_DISPLAY_REMOVED,               /**< Display has been removed from the system */
    THALAMUS_SDL_EVENT_DISPLAY_MOVED,                 /**< Display has changed position */
    THALAMUS_SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED,  /**< Display has changed desktop mode */
    THALAMUS_SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED,  /**< Display has changed current mode */
    THALAMUS_SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED, /**< Display has changed content scale */
    THALAMUS_SDL_EVENT_DISPLAY_USABLE_BOUNDS_CHANGED, /**< Display has changed usable bounds */
    THALAMUS_SDL_EVENT_DISPLAY_FIRST = THALAMUS_SDL_EVENT_DISPLAY_ORIENTATION,
    THALAMUS_SDL_EVENT_DISPLAY_LAST = THALAMUS_SDL_EVENT_DISPLAY_USABLE_BOUNDS_CHANGED,

    /* Window events */
    /* 0x200 was THALAMUS_SDL_WINDOWEVENT, reserve the number for sdl2-compat */
    /* 0x201 was THALAMUS_SDL_SYSWMEVENT, reserve the number for sdl2-compat */
    THALAMUS_SDL_EVENT_WINDOW_SHOWN = 0x202,     /**< Window has been shown */
    THALAMUS_SDL_EVENT_WINDOW_HIDDEN,            /**< Window has been hidden */
    THALAMUS_SDL_EVENT_WINDOW_EXPOSED,           /**< Window has been exposed and should be redrawn, and can be redrawn directly from event watchers for this event.
                                             data1 is 1 for live-resize expose events, 0 otherwise. */
    THALAMUS_SDL_EVENT_WINDOW_MOVED,             /**< Window has been moved to data1, data2 */
    THALAMUS_SDL_EVENT_WINDOW_RESIZED,           /**< Window has been resized to data1xdata2 */
    THALAMUS_SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED,/**< The pixel size of the window has changed to data1xdata2 */
    THALAMUS_SDL_EVENT_WINDOW_METAL_VIEW_RESIZED,/**< The pixel size of a Metal view associated with the window has changed */
    THALAMUS_SDL_EVENT_WINDOW_MINIMIZED,         /**< Window has been minimized */
    THALAMUS_SDL_EVENT_WINDOW_MAXIMIZED,         /**< Window has been maximized */
    THALAMUS_SDL_EVENT_WINDOW_RESTORED,          /**< Window has been restored to normal size and position */
    THALAMUS_SDL_EVENT_WINDOW_MOUSE_ENTER,       /**< Window has gained mouse focus */
    THALAMUS_SDL_EVENT_WINDOW_MOUSE_LEAVE,       /**< Window has lost mouse focus */
    THALAMUS_SDL_EVENT_WINDOW_FOCUS_GAINED,      /**< Window has gained keyboard focus */
    THALAMUS_SDL_EVENT_WINDOW_FOCUS_LOST,        /**< Window has lost keyboard focus */
    THALAMUS_SDL_EVENT_WINDOW_CLOSE_REQUESTED,   /**< The window manager requests that the window be closed */
    THALAMUS_SDL_EVENT_WINDOW_HIT_TEST,          /**< Window had a hit test that wasn't THALAMUS_SDL_HITTEST_NORMAL */
    THALAMUS_SDL_EVENT_WINDOW_ICCPROF_CHANGED,   /**< The window's ICC profile has changed */
    THALAMUS_SDL_EVENT_WINDOW_DISPLAY_CHANGED,   /**< Window has been moved to display data1 */
    THALAMUS_SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED, /**< Window display scale has been changed */
    THALAMUS_SDL_EVENT_WINDOW_SAFE_AREA_CHANGED, /**< The window safe area has been changed */
    THALAMUS_SDL_EVENT_WINDOW_OCCLUDED,          /**< The window has been occluded */
    THALAMUS_SDL_EVENT_WINDOW_ENTER_FULLSCREEN,  /**< The window has entered fullscreen mode */
    THALAMUS_SDL_EVENT_WINDOW_LEAVE_FULLSCREEN,  /**< The window has left fullscreen mode */
    THALAMUS_SDL_EVENT_WINDOW_DESTROYED,         /**< The window with the associated ID is being or has been destroyed. If this message is being handled
                                             in an event watcher, the window handle is still valid and can still be used to retrieve any properties
                                             associated with the window. Otherwise, the handle has already been destroyed and all resources
                                             associated with it are invalid */
    THALAMUS_SDL_EVENT_WINDOW_HDR_STATE_CHANGED, /**< Window HDR properties have changed */
    THALAMUS_SDL_EVENT_WINDOW_SETTINGS_CHANGED,  /**< Window settings have changed (on visionOS) */
    THALAMUS_SDL_EVENT_WINDOW_FIRST = THALAMUS_SDL_EVENT_WINDOW_SHOWN,
    THALAMUS_SDL_EVENT_WINDOW_LAST = THALAMUS_SDL_EVENT_WINDOW_SETTINGS_CHANGED,

    /* Keyboard events */
    THALAMUS_SDL_EVENT_KEY_DOWN        = 0x300, /**< Key pressed */
    THALAMUS_SDL_EVENT_KEY_UP,                  /**< Key released */
    THALAMUS_SDL_EVENT_TEXT_EDITING,            /**< Keyboard text editing (composition) */
    THALAMUS_SDL_EVENT_TEXT_INPUT,              /**< Keyboard text input */
    THALAMUS_SDL_EVENT_KEYMAP_CHANGED,          /**< Keymap changed due to a system event such as an
                                            input language or keyboard layout change. */
    THALAMUS_SDL_EVENT_KEYBOARD_ADDED,          /**< A new keyboard has been inserted into the system */
    THALAMUS_SDL_EVENT_KEYBOARD_REMOVED,        /**< A keyboard has been removed */
    THALAMUS_SDL_EVENT_TEXT_EDITING_CANDIDATES, /**< Keyboard text editing candidates */
    THALAMUS_SDL_EVENT_SCREEN_KEYBOARD_SHOWN,   /**< The on-screen keyboard has been shown */
    THALAMUS_SDL_EVENT_SCREEN_KEYBOARD_HIDDEN,  /**< The on-screen keyboard has been hidden */
    THALAMUS_SDL_EVENT_KEYBOARD_FIRST = THALAMUS_SDL_EVENT_KEY_DOWN,
    THALAMUS_SDL_EVENT_KEYBOARD_LAST = THALAMUS_SDL_EVENT_SCREEN_KEYBOARD_HIDDEN,

    /* Mouse events */
    THALAMUS_SDL_EVENT_MOUSE_MOTION    = 0x400, /**< Mouse moved */
    THALAMUS_SDL_EVENT_MOUSE_BUTTON_DOWN,       /**< Mouse button pressed */
    THALAMUS_SDL_EVENT_MOUSE_BUTTON_UP,         /**< Mouse button released */
    THALAMUS_SDL_EVENT_MOUSE_WHEEL,             /**< Mouse wheel motion */
    THALAMUS_SDL_EVENT_MOUSE_ADDED,             /**< A new mouse has been inserted into the system */
    THALAMUS_SDL_EVENT_MOUSE_REMOVED,           /**< A mouse has been removed */
    THALAMUS_SDL_EVENT_MOUSE_FIRST = THALAMUS_SDL_EVENT_MOUSE_MOTION,
    THALAMUS_SDL_EVENT_MOUSE_LAST = THALAMUS_SDL_EVENT_MOUSE_REMOVED,

    /* Joystick events */
    THALAMUS_SDL_EVENT_JOYSTICK_AXIS_MOTION  = 0x600, /**< Joystick axis motion */
    THALAMUS_SDL_EVENT_JOYSTICK_BALL_MOTION,          /**< Joystick trackball motion */
    THALAMUS_SDL_EVENT_JOYSTICK_HAT_MOTION,           /**< Joystick hat position change */
    THALAMUS_SDL_EVENT_JOYSTICK_BUTTON_DOWN,          /**< Joystick button pressed */
    THALAMUS_SDL_EVENT_JOYSTICK_BUTTON_UP,            /**< Joystick button released */
    THALAMUS_SDL_EVENT_JOYSTICK_ADDED,                /**< A new joystick has been inserted into the system */
    THALAMUS_SDL_EVENT_JOYSTICK_REMOVED,              /**< An opened joystick has been removed */
    THALAMUS_SDL_EVENT_JOYSTICK_BATTERY_UPDATED,      /**< Joystick battery level change */
    THALAMUS_SDL_EVENT_JOYSTICK_UPDATE_COMPLETE,      /**< Joystick update is complete */
    THALAMUS_SDL_EVENT_JOYSTICK_FIRST = THALAMUS_SDL_EVENT_JOYSTICK_AXIS_MOTION,
    THALAMUS_SDL_EVENT_JOYSTICK_LAST = THALAMUS_SDL_EVENT_JOYSTICK_UPDATE_COMPLETE,

    /* Gamepad events */
    THALAMUS_SDL_EVENT_GAMEPAD_AXIS_MOTION  = 0x650, /**< Gamepad axis motion */
    THALAMUS_SDL_EVENT_GAMEPAD_BUTTON_DOWN,          /**< Gamepad button pressed */
    THALAMUS_SDL_EVENT_GAMEPAD_BUTTON_UP,            /**< Gamepad button released */
    THALAMUS_SDL_EVENT_GAMEPAD_ADDED,                /**< A new gamepad has been inserted into the system */
    THALAMUS_SDL_EVENT_GAMEPAD_REMOVED,              /**< A gamepad has been removed */
    THALAMUS_SDL_EVENT_GAMEPAD_REMAPPED,             /**< The gamepad mapping was updated */
    THALAMUS_SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN,        /**< Gamepad touchpad was touched */
    THALAMUS_SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION,      /**< Gamepad touchpad finger was moved */
    THALAMUS_SDL_EVENT_GAMEPAD_TOUCHPAD_UP,          /**< Gamepad touchpad finger was lifted */
    THALAMUS_SDL_EVENT_GAMEPAD_SENSOR_UPDATE,        /**< Gamepad sensor was updated */
    THALAMUS_SDL_EVENT_GAMEPAD_UPDATE_COMPLETE,      /**< Gamepad update is complete */
    THALAMUS_SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED,  /**< Gamepad Steam handle has changed */
    THALAMUS_SDL_EVENT_GAMEPAD_CAPSENSE_TOUCH,       /**< Gamepad capsense was touched */
    THALAMUS_SDL_EVENT_GAMEPAD_CAPSENSE_RELEASE,     /**< Gamepad capsense was released */
    THALAMUS_SDL_EVENT_GAMEPAD_FIRST = THALAMUS_SDL_EVENT_GAMEPAD_AXIS_MOTION,
    THALAMUS_SDL_EVENT_GAMEPAD_LAST = THALAMUS_SDL_EVENT_GAMEPAD_CAPSENSE_RELEASE,

    /* Touch events */
    THALAMUS_SDL_EVENT_FINGER_DOWN      = 0x700,
    THALAMUS_SDL_EVENT_FINGER_UP,
    THALAMUS_SDL_EVENT_FINGER_MOTION,
    THALAMUS_SDL_EVENT_FINGER_CANCELED,
    THALAMUS_SDL_EVENT_FINGER_FIRST = THALAMUS_SDL_EVENT_FINGER_DOWN,
    THALAMUS_SDL_EVENT_FINGER_LAST = THALAMUS_SDL_EVENT_FINGER_CANCELED,

    /* Pinch events */
    THALAMUS_SDL_EVENT_PINCH_BEGIN      = 0x710,     /**< Pinch gesture started */
    THALAMUS_SDL_EVENT_PINCH_UPDATE,                 /**< Pinch gesture updated */
    THALAMUS_SDL_EVENT_PINCH_END,                    /**< Pinch gesture ended */
    THALAMUS_SDL_EVENT_PINCH_FIRST = THALAMUS_SDL_EVENT_PINCH_BEGIN,
    THALAMUS_SDL_EVENT_PINCH_LAST = THALAMUS_SDL_EVENT_PINCH_END,

    /* 0x800, 0x801, and 0x802 were the Gesture events from SDL2. Do not reuse these values! sdl2-compat needs them! */

    /* Clipboard events */
    THALAMUS_SDL_EVENT_CLIPBOARD_UPDATE = 0x900, /**< The clipboard changed */
    THALAMUS_SDL_EVENT_CLIPBOARD_FIRST = THALAMUS_SDL_EVENT_CLIPBOARD_UPDATE,
    THALAMUS_SDL_EVENT_CLIPBOARD_LAST = THALAMUS_SDL_EVENT_CLIPBOARD_UPDATE,

    /* Drag and drop events */
    THALAMUS_SDL_EVENT_DROP_FILE        = 0x1000, /**< The system requests a file open */
    THALAMUS_SDL_EVENT_DROP_TEXT,                 /**< text/plain drag-and-drop event */
    THALAMUS_SDL_EVENT_DROP_BEGIN,                /**< A new set of drops is beginning (NULL filename) */
    THALAMUS_SDL_EVENT_DROP_COMPLETE,             /**< Current set of drops is now complete (NULL filename) */
    THALAMUS_SDL_EVENT_DROP_POSITION,             /**< Position while moving over the window */
    THALAMUS_SDL_EVENT_DROP_FIRST = THALAMUS_SDL_EVENT_DROP_FILE,
    THALAMUS_SDL_EVENT_DROP_LAST = THALAMUS_SDL_EVENT_DROP_POSITION,

    /* Audio hotplug events */
    THALAMUS_SDL_EVENT_AUDIO_DEVICE_ADDED = 0x1100,  /**< A new audio device is available */
    THALAMUS_SDL_EVENT_AUDIO_DEVICE_REMOVED,         /**< An audio device has been removed. */
    THALAMUS_SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED,  /**< An audio device's format has been changed by the system. */
    THALAMUS_SDL_EVENT_AUDIO_DEVICE_FIRST = THALAMUS_SDL_EVENT_AUDIO_DEVICE_ADDED,
    THALAMUS_SDL_EVENT_AUDIO_DEVICE_LAST = THALAMUS_SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED,

    /* Sensor events */
    THALAMUS_SDL_EVENT_SENSOR_UPDATE = 0x1200,     /**< A sensor was updated */
    THALAMUS_SDL_EVENT_SENSOR_FIRST = THALAMUS_SDL_EVENT_SENSOR_UPDATE,
    THALAMUS_SDL_EVENT_SENSOR_LAST = THALAMUS_SDL_EVENT_SENSOR_UPDATE,

    /* Pressure-sensitive pen events */
    THALAMUS_SDL_EVENT_PEN_PROXIMITY_IN = 0x1300,  /**< Pressure-sensitive pen has become available */
    THALAMUS_SDL_EVENT_PEN_PROXIMITY_OUT,          /**< Pressure-sensitive pen has become unavailable */
    THALAMUS_SDL_EVENT_PEN_DOWN,                   /**< Pressure-sensitive pen touched drawing surface */
    THALAMUS_SDL_EVENT_PEN_UP,                     /**< Pressure-sensitive pen stopped touching drawing surface */
    THALAMUS_SDL_EVENT_PEN_BUTTON_DOWN,            /**< Pressure-sensitive pen button pressed */
    THALAMUS_SDL_EVENT_PEN_BUTTON_UP,              /**< Pressure-sensitive pen button released */
    THALAMUS_SDL_EVENT_PEN_MOTION,                 /**< Pressure-sensitive pen is moving on the tablet */
    THALAMUS_SDL_EVENT_PEN_AXIS,                   /**< Pressure-sensitive pen angle/pressure/etc changed */
    THALAMUS_SDL_EVENT_PEN_FIRST = THALAMUS_SDL_EVENT_PEN_PROXIMITY_IN,
    THALAMUS_SDL_EVENT_PEN_LAST = THALAMUS_SDL_EVENT_PEN_AXIS,

    /* Camera hotplug events */
    THALAMUS_SDL_EVENT_CAMERA_DEVICE_ADDED = 0x1400,  /**< A new camera device is available */
    THALAMUS_SDL_EVENT_CAMERA_DEVICE_REMOVED,         /**< A camera device has been removed. */
    THALAMUS_SDL_EVENT_CAMERA_DEVICE_APPROVED,        /**< A camera device has been approved for use by the user. */
    THALAMUS_SDL_EVENT_CAMERA_DEVICE_DENIED,          /**< A camera device has been denied for use by the user. */
    THALAMUS_SDL_EVENT_CAMERA_DEVICE_FIRST = THALAMUS_SDL_EVENT_CAMERA_DEVICE_ADDED,
    THALAMUS_SDL_EVENT_CAMERA_DEVICE_LAST = THALAMUS_SDL_EVENT_CAMERA_DEVICE_DENIED,

    /* Notification events */
    THALAMUS_SDL_EVENT_NOTIFICATION_ACTION_INVOKED = 0x1500, /**< A user response to a system notification was received. */
    THALAMUS_SDL_EVENT_NOTIFICATION_FIRST = THALAMUS_SDL_EVENT_NOTIFICATION_ACTION_INVOKED,
    THALAMUS_SDL_EVENT_NOTIFICATION_LAST = THALAMUS_SDL_EVENT_NOTIFICATION_ACTION_INVOKED,

    /* Render events */
    THALAMUS_SDL_EVENT_RENDER_TARGETS_RESET = 0x2000, /**< The render targets have been reset and their contents need to be updated */
    THALAMUS_SDL_EVENT_RENDER_DEVICE_RESET, /**< The device has been reset and all textures need to be recreated */
    THALAMUS_SDL_EVENT_RENDER_DEVICE_LOST, /**< The device has been lost and can't be recovered. */
    THALAMUS_SDL_EVENT_RENDER_FIRST = THALAMUS_SDL_EVENT_RENDER_TARGETS_RESET,
    THALAMUS_SDL_EVENT_RENDER_LAST = THALAMUS_SDL_EVENT_RENDER_DEVICE_LOST,

    /* Reserved events for private platforms */
    THALAMUS_SDL_EVENT_PRIVATE0 = 0x4000,
    THALAMUS_SDL_EVENT_PRIVATE1,
    THALAMUS_SDL_EVENT_PRIVATE2,
    THALAMUS_SDL_EVENT_PRIVATE3,

    /* Internal events */
    THALAMUS_SDL_EVENT_POLL_SENTINEL = 0x7F00, /**< Signals the end of an event poll cycle */

    /** Events THALAMUS_SDL_EVENT_USER through THALAMUS_SDL_EVENT_LAST are for your use,
     *  and should be allocated with THALAMUS_SDL_RegisterEvents()
     */
    THALAMUS_SDL_EVENT_USER    = 0x8000,

    /**
     *  This last event is only for bounding internal arrays
     */
    THALAMUS_SDL_EVENT_LAST    = 0xFFFF,

    /* This just makes sure the enum is the size of Uint32 */
    THALAMUS_SDL_EVENT_ENUM_PADDING = 0x7FFFFFFF

} THALAMUS_SDL_EventType;

typedef enum THALAMUS_SDL_PowerState
{
    THALAMUS_SDL_POWERSTATE_ERROR = -1,   /**< error determining power status */
    THALAMUS_SDL_POWERSTATE_UNKNOWN,      /**< cannot determine power status */
    THALAMUS_SDL_POWERSTATE_ON_BATTERY,   /**< Not plugged in, running on the battery */
    THALAMUS_SDL_POWERSTATE_NO_BATTERY,   /**< Plugged in, no battery available */
    THALAMUS_SDL_POWERSTATE_CHARGING,     /**< Plugged in, charging battery */
    THALAMUS_SDL_POWERSTATE_CHARGED       /**< Plugged in, battery charged */
} THALAMUS_SDL_PowerState;

typedef enum THALAMUS_SDL_PenAxis
{
    THALAMUS_SDL_PEN_AXIS_PRESSURE,  /**< Pen pressure.  Unidirectional: 0 to 1.0 */
    THALAMUS_SDL_PEN_AXIS_XTILT,     /**< Pen horizontal tilt angle.  Bidirectional: -90.0 to 90.0 (left-to-right). */
    THALAMUS_SDL_PEN_AXIS_YTILT,     /**< Pen vertical tilt angle.  Bidirectional: -90.0 to 90.0 (top-to-down). */
    THALAMUS_SDL_PEN_AXIS_DISTANCE,  /**< Pen distance to drawing surface.  Unidirectional: 0.0 to 1.0 */
    THALAMUS_SDL_PEN_AXIS_ROTATION,  /**< Pen barrel rotation.  Bidirectional: -180 to 179.9 (clockwise, 0 is facing up, -180.0 is facing down). */
    THALAMUS_SDL_PEN_AXIS_SLIDER,    /**< Pen finger wheel or slider (e.g., Airbrush Pen).  Unidirectional: 0 to 1.0 */
    THALAMUS_SDL_PEN_AXIS_TANGENTIAL_PRESSURE,    /**< Pressure from squeezing the pen ("barrel pressure"). */
    THALAMUS_SDL_PEN_AXIS_COUNT       /**< Total known pen axis types in this version of SDL. This number may grow in future releases! */
} THALAMUS_SDL_PenAxis;

typedef int8_t Sint8;
typedef int16_t Sint16;
typedef int32_t Sint32;
typedef int64_t Sint64;

typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint64_t Uint64;

typedef Uint32 THALAMUS_SDL_DisplayID;
typedef Uint32 THALAMUS_SDL_WindowID;
typedef Uint32 THALAMUS_SDL_KeyboardID;
typedef Uint32 THALAMUS_SDL_Keycode;
typedef Uint16 THALAMUS_SDL_Keymod;
typedef Uint32 THALAMUS_SDL_MouseID;
typedef Uint32 THALAMUS_SDL_MouseButtonFlags;
typedef Uint32 THALAMUS_SDL_JoystickID;
typedef Uint32 THALAMUS_SDL_AudioDeviceID;
typedef Uint32 THALAMUS_SDL_CameraID;
typedef Uint32 THALAMUS_SDL_NotificationID;
typedef Uint64 THALAMUS_SDL_TouchID;
typedef Uint64 THALAMUS_SDL_FingerID;
typedef Uint32 THALAMUS_SDL_PenID;
typedef Uint32 THALAMUS_SDL_PenInputFlags;
typedef Uint32 THALAMUS_SDL_SensorID;

/**
 * Fields shared by every event (event.common.*)
 *
 * All the individual structs that comprise the THALAMUS_SDL_Event union start with
 * these same fields, so you can access them from any struct directly.
 *
 * Event types that don't have further data in a specific struct will still
 * have valid CommonEvent data, accessible via the event.common field.
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_CommonEvent
{
    Uint32 type;        /**< Event type, shared with all events, Uint32 to cover user events which are not in the THALAMUS_SDL_EventType enumeration */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
} THALAMUS_SDL_CommonEvent;

/**
 * Display state change event data (event.display.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_DisplayEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_DISPLAY_* */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_DisplayID displayID;/**< The associated display */
    Sint32 data1;       /**< event dependent data */
    Sint32 data2;       /**< event dependent data */
} THALAMUS_SDL_DisplayEvent;

/**
 * Window state change event data (event.window.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_WindowEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_WINDOW_* */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_WindowID windowID; /**< The associated window */
    Sint32 data1;       /**< event dependent data */
    Sint32 data2;       /**< event dependent data */
} THALAMUS_SDL_WindowEvent;

/**
 * Keyboard device event structure (event.kdevice.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_KeyboardDeviceEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_KEYBOARD_ADDED or THALAMUS_SDL_EVENT_KEYBOARD_REMOVED */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_KeyboardID which;   /**< The keyboard instance id */
} THALAMUS_SDL_KeyboardDeviceEvent;

/**
 * Keyboard button event structure (event.key.*)
 *
 * The `key` is the base THALAMUS_SDL_Keycode generated by pressing the `scancode`
 * using the current keyboard layout, applying any options specified in
 * THALAMUS_SDL_HINT_KEYCODE_OPTIONS. You can get the THALAMUS_SDL_Keycode corresponding to the
 * event scancode and modifiers directly from the keyboard layout, bypassing
 * THALAMUS_SDL_HINT_KEYCODE_OPTIONS, by calling THALAMUS_SDL_GetKeyFromScancode().
 *
 * \since This struct is available since SDL 3.2.0.
 *
 * \sa THALAMUS_SDL_GetKeyFromScancode
 * \sa THALAMUS_SDL_HINT_KEYCODE_OPTIONS
 */
typedef struct THALAMUS_SDL_KeyboardEvent
{
    THALAMUS_SDL_EventType type;     /**< THALAMUS_SDL_EVENT_KEY_DOWN or THALAMUS_SDL_EVENT_KEY_UP */
    Uint32 reserved;
    Uint64 timestamp;       /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_WindowID windowID;  /**< The window with keyboard focus, if any */
    THALAMUS_SDL_KeyboardID which;   /**< The keyboard instance id, or 0 if unknown or virtual */
    THALAMUS_SDL_Scancode scancode;  /**< SDL physical key code */
    THALAMUS_SDL_Keycode key;        /**< SDL virtual key code */
    THALAMUS_SDL_Keymod mod;         /**< current key modifiers */
    Uint16 raw;             /**< The platform dependent scancode for this event */
    bool down;              /**< true if the key is pressed */
    bool repeat;            /**< true if this is a key repeat */
} THALAMUS_SDL_KeyboardEvent;

/**
 * Keyboard text editing event structure (event.edit.*)
 *
 * The start cursor is the position, in UTF-8 characters, where new typing
 * will be inserted into the editing text. The length is the number of UTF-8
 * characters that will be replaced by new typing.
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_TextEditingEvent
{
    THALAMUS_SDL_EventType type;         /**< THALAMUS_SDL_EVENT_TEXT_EDITING */
    Uint32 reserved;
    Uint64 timestamp;           /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_WindowID windowID;      /**< The window with keyboard focus, if any */
    const char *text;           /**< The editing text */
    Sint32 start;               /**< The start cursor of selected editing text, or -1 if not set */
    Sint32 length;              /**< The length of selected editing text, or -1 if not set */
} THALAMUS_SDL_TextEditingEvent;

/**
 * Keyboard IME candidates event structure (event.edit_candidates.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_TextEditingCandidatesEvent
{
    THALAMUS_SDL_EventType type;         /**< THALAMUS_SDL_EVENT_TEXT_EDITING_CANDIDATES */
    Uint32 reserved;
    Uint64 timestamp;           /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_WindowID windowID;      /**< The window with keyboard focus, if any */
    const char * const *candidates;    /**< The list of candidates, or NULL if there are no candidates available */
    Sint32 num_candidates;      /**< The number of strings in `candidates` */
    Sint32 selected_candidate;  /**< The index of the selected candidate, or -1 if no candidate is selected */
    bool horizontal;          /**< true if the list is horizontal, false if it's vertical */
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
} THALAMUS_SDL_TextEditingCandidatesEvent;

/**
 * Keyboard text input event structure (event.text.*)
 *
 * This event will never be delivered unless text input is enabled by calling
 * THALAMUS_SDL_StartTextInput(). Text input is disabled by default!
 *
 * \since This struct is available since SDL 3.2.0.
 *
 * \sa THALAMUS_SDL_StartTextInput
 * \sa THALAMUS_SDL_StopTextInput
 */
typedef struct THALAMUS_SDL_TextInputEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_TEXT_INPUT */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_WindowID windowID; /**< The window with keyboard focus, if any */
    const char *text;   /**< The input text, UTF-8 encoded */
} THALAMUS_SDL_TextInputEvent;

/**
 * Mouse device event structure (event.mdevice.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_MouseDeviceEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_MOUSE_ADDED or THALAMUS_SDL_EVENT_MOUSE_REMOVED */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_MouseID which;  /**< The mouse instance id */
} THALAMUS_SDL_MouseDeviceEvent;

/**
 * Mouse motion event structure (event.motion.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_MouseMotionEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_MOUSE_MOTION */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_WindowID windowID; /**< The window with mouse focus, if any */
    THALAMUS_SDL_MouseID which;  /**< The mouse instance id in relative mode, THALAMUS_SDL_TOUCH_MOUSEID for touch events, THALAMUS_SDL_PEN_MOUSEID for pen events, or 0 */
    THALAMUS_SDL_MouseButtonFlags state;       /**< The current button state */
    float x;            /**< X coordinate, relative to window */
    float y;            /**< Y coordinate, relative to window */
    float xrel;         /**< The relative motion in the X direction */
    float yrel;         /**< The relative motion in the Y direction */
} THALAMUS_SDL_MouseMotionEvent;

/**
 * Mouse button event structure (event.button.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_MouseButtonEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_MOUSE_BUTTON_DOWN or THALAMUS_SDL_EVENT_MOUSE_BUTTON_UP */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_WindowID windowID; /**< The window with mouse focus, if any */
    THALAMUS_SDL_MouseID which;  /**< The mouse instance id in relative mode, THALAMUS_SDL_TOUCH_MOUSEID for touch events, or 0 */
    Uint8 button;       /**< The mouse button index */
    bool down;          /**< true if the button is pressed */
    Uint8 clicks;       /**< 1 for single-click, 2 for double-click, etc. */
    Uint8 padding;
    float x;            /**< X coordinate, relative to window */
    float y;            /**< Y coordinate, relative to window */
} THALAMUS_SDL_MouseButtonEvent;

/**
 * Mouse wheel event structure (event.wheel.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_MouseWheelEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_MOUSE_WHEEL */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_WindowID windowID; /**< The window with mouse focus, if any */
    THALAMUS_SDL_MouseID which;  /**< The mouse instance id in relative mode or 0 */
    float x;            /**< The amount scrolled horizontally, positive to the right and negative to the left */
    float y;            /**< The amount scrolled vertically, positive away from the user and negative toward the user */
    THALAMUS_SDL_MouseWheelDirection direction; /**< Set to one of the THALAMUS_SDL_MOUSEWHEEL_* defines. When FLIPPED the values in X and Y will be opposite. Multiply by -1 to change them back */
    float mouse_x;      /**< X coordinate, relative to window */
    float mouse_y;      /**< Y coordinate, relative to window */
    Sint32 integer_x;   /**< The amount scrolled horizontally, accumulated to whole scroll "ticks" (added in 3.2.12) */
    Sint32 integer_y;   /**< The amount scrolled vertically, accumulated to whole scroll "ticks" (added in 3.2.12) */
} THALAMUS_SDL_MouseWheelEvent;

/**
 * Joystick axis motion event structure (event.jaxis.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_JoyAxisEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_JOYSTICK_AXIS_MOTION */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_JoystickID which; /**< The joystick instance id */
    Uint8 axis;         /**< The joystick axis index */
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
    Sint16 value;       /**< The axis value (range: -32768 to 32767) */
    Uint16 padding4;
} THALAMUS_SDL_JoyAxisEvent;

/**
 * Joystick trackball motion event structure (event.jball.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_JoyBallEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_JOYSTICK_BALL_MOTION */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_JoystickID which; /**< The joystick instance id */
    Uint8 ball;         /**< The joystick trackball index */
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
    Sint16 xrel;        /**< The relative motion in the X direction */
    Sint16 yrel;        /**< The relative motion in the Y direction */
} THALAMUS_SDL_JoyBallEvent;

/**
 * Joystick hat position change event structure (event.jhat.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_JoyHatEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_JOYSTICK_HAT_MOTION */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_JoystickID which; /**< The joystick instance id */
    Uint8 hat;          /**< The joystick hat index */
    Uint8 value;        /**< The hat position value.
                         *   \sa THALAMUS_SDL_HAT_LEFTUP THALAMUS_SDL_HAT_UP THALAMUS_SDL_HAT_RIGHTUP
                         *   \sa THALAMUS_SDL_HAT_LEFT THALAMUS_SDL_HAT_CENTERED THALAMUS_SDL_HAT_RIGHT
                         *   \sa THALAMUS_SDL_HAT_LEFTDOWN THALAMUS_SDL_HAT_DOWN THALAMUS_SDL_HAT_RIGHTDOWN
                         *
                         *   Note that zero means the POV is centered.
                         */
    Uint8 padding1;
    Uint8 padding2;
} THALAMUS_SDL_JoyHatEvent;

/**
 * Joystick button event structure (event.jbutton.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_JoyButtonEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_JOYSTICK_BUTTON_DOWN or THALAMUS_SDL_EVENT_JOYSTICK_BUTTON_UP */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_JoystickID which; /**< The joystick instance id */
    Uint8 button;       /**< The joystick button index */
    bool down;      /**< true if the button is pressed */
    Uint8 padding1;
    Uint8 padding2;
} THALAMUS_SDL_JoyButtonEvent;

/**
 * Joystick device event structure (event.jdevice.*)
 *
 * SDL will send JOYSTICK_ADDED events for devices that are already plugged in
 * during THALAMUS_SDL_Init.
 *
 * \since This struct is available since SDL 3.2.0.
 *
 * \sa THALAMUS_SDL_GamepadDeviceEvent
 */
typedef struct THALAMUS_SDL_JoyDeviceEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_JOYSTICK_ADDED or THALAMUS_SDL_EVENT_JOYSTICK_REMOVED or THALAMUS_SDL_EVENT_JOYSTICK_UPDATE_COMPLETE */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_JoystickID which;       /**< The joystick instance id */
} THALAMUS_SDL_JoyDeviceEvent;

/**
 * Joystick battery level change event structure (event.jbattery.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_JoyBatteryEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_JOYSTICK_BATTERY_UPDATED */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_JoystickID which; /**< The joystick instance id */
    THALAMUS_SDL_PowerState state; /**< The joystick battery state */
    int percent;          /**< The joystick battery percent charge remaining */
} THALAMUS_SDL_JoyBatteryEvent;

/**
 * Gamepad axis motion event structure (event.gaxis.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_GamepadAxisEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_GAMEPAD_AXIS_MOTION */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_JoystickID which; /**< The joystick instance id */
    Uint8 axis;         /**< The gamepad axis (THALAMUS_SDL_GamepadAxis) */
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
    Sint16 value;       /**< The axis value (range: -32768 to 32767) */
    Uint16 padding4;
} THALAMUS_SDL_GamepadAxisEvent;


/**
 * Gamepad button event structure (event.gbutton.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_GamepadButtonEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_GAMEPAD_BUTTON_DOWN or THALAMUS_SDL_EVENT_GAMEPAD_BUTTON_UP */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_JoystickID which; /**< The joystick instance id */
    Uint8 button;       /**< The gamepad button (THALAMUS_SDL_GamepadButton) */
    bool down;      /**< true if the button is pressed */
    Uint8 padding1;
    Uint8 padding2;
} THALAMUS_SDL_GamepadButtonEvent;


/**
 * Gamepad device event structure (event.gdevice.*)
 *
 * Joysticks that are supported gamepads receive both an THALAMUS_SDL_JoyDeviceEvent
 * and an THALAMUS_SDL_GamepadDeviceEvent.
 *
 * SDL will send GAMEPAD_ADDED events for joysticks that are already plugged
 * in during THALAMUS_SDL_Init() and are recognized as gamepads. It will also send
 * events for joysticks that get gamepad mappings at runtime.
 *
 * \since This struct is available since SDL 3.2.0.
 *
 * \sa THALAMUS_SDL_JoyDeviceEvent
 */
typedef struct THALAMUS_SDL_GamepadDeviceEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_GAMEPAD_ADDED, THALAMUS_SDL_EVENT_GAMEPAD_REMOVED, or THALAMUS_SDL_EVENT_GAMEPAD_REMAPPED, THALAMUS_SDL_EVENT_GAMEPAD_UPDATE_COMPLETE or THALAMUS_SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_JoystickID which;       /**< The joystick instance id */
} THALAMUS_SDL_GamepadDeviceEvent;

/**
 * Gamepad touchpad event structure (event.gtouchpad.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_GamepadTouchpadEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN or THALAMUS_SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION or THALAMUS_SDL_EVENT_GAMEPAD_TOUCHPAD_UP */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_JoystickID which; /**< The joystick instance id */
    Sint32 touchpad;    /**< The index of the touchpad */
    Sint32 finger;      /**< The index of the finger on the touchpad */
    float x;            /**< Normalized in the range 0...1 with 0 being on the left */
    float y;            /**< Normalized in the range 0...1 with 0 being at the top */
    float pressure;     /**< Normalized in the range 0...1 */
} THALAMUS_SDL_GamepadTouchpadEvent;

/**
 * Gamepad sensor event structure (event.gsensor.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_GamepadSensorEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_GAMEPAD_SENSOR_UPDATE */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_JoystickID which; /**< The joystick instance id */
    Sint32 sensor;      /**< The type of the sensor, one of the values of THALAMUS_SDL_SensorType */
    float data[3];      /**< Up to 3 values from the sensor, as defined in THALAMUS_SDL_sensor.h */
    Uint64 sensor_timestamp; /**< The timestamp of the sensor reading in nanoseconds, not necessarily synchronized with the system clock */
} THALAMUS_SDL_GamepadSensorEvent;

/**
 * Gamepad capsense event structure (event.gcapsense.*)
 *
 * \since This struct is available since SDL 3.6.0.
 */
typedef struct THALAMUS_SDL_GamepadCapSenseEvent
{
    THALAMUS_SDL_EventType type;     /**< THALAMUS_SDL_EVENT_GAMEPAD_CAPSENSE_TOUCH or THALAMUS_SDL_EVENT_GAMEPAD_CAPSENSE_RELEASE */
    Uint32 reserved;
    Uint64 timestamp;       /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_JoystickID which;   /**< The joystick instance id */
    Uint8 capsense;         /**< The capsense type (THALAMUS_SDL_GamepadCapSenseType) */
    bool down;              /**< true if the capsense is touched */
    Uint8 padding1;
    Uint8 padding2;
} THALAMUS_SDL_GamepadCapSenseEvent;

/**
 * Audio device event structure (event.adevice.*)
 *
 * Note that SDL will send a THALAMUS_SDL_EVENT_AUDIO_DEVICE_ADDED event for every
 * device it discovers during initialization. After that, this event will only
 * arrive when a device is hotplugged during the program's run.
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_AudioDeviceEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_AUDIO_DEVICE_ADDED, or THALAMUS_SDL_EVENT_AUDIO_DEVICE_REMOVED, or THALAMUS_SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_AudioDeviceID which;       /**< THALAMUS_SDL_AudioDeviceID for the device being added or removed or changing */
    bool recording; /**< false if a playback device, true if a recording device. */
    Uint8 padding1;
    Uint8 padding2;
    Uint8 padding3;
} THALAMUS_SDL_AudioDeviceEvent;

/**
 * Camera device event structure (event.cdevice.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_CameraDeviceEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_CAMERA_DEVICE_ADDED, THALAMUS_SDL_EVENT_CAMERA_DEVICE_REMOVED, THALAMUS_SDL_EVENT_CAMERA_DEVICE_APPROVED, THALAMUS_SDL_EVENT_CAMERA_DEVICE_DENIED */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_CameraID which;       /**< THALAMUS_SDL_CameraID for the device being added or removed or changing */
} THALAMUS_SDL_CameraDeviceEvent;

/**
 * Notification dialog event structure (event.notification.*)
 *
 * An `action_id` value of 'default' for an
 * THALAMUS_SDL_EVENT_NOTIFICATION_ACTION_INVOKED event indicates that the notification
 * was interacted with without selecting a specific action (e.g. the body of
 * the notification was clicked on).
 *
 * \since This struct is available since SDL 3.6.0.
 */
typedef struct THALAMUS_SDL_NotificationEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_NOTIFICATION_ACTION_INVOKED */
    Uint32 reserved;
    Uint64 timestamp;         /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_NotificationID which; /**< The ID of the notification that generated this event. */
    const char *action_id;    /**< The identifier string of the action invoked in the notification dialog. */
} THALAMUS_SDL_NotificationEvent;

/**
 * Renderer event structure (event.render.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_RenderEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_RENDER_TARGETS_RESET, THALAMUS_SDL_EVENT_RENDER_DEVICE_RESET, THALAMUS_SDL_EVENT_RENDER_DEVICE_LOST */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_WindowID windowID; /**< The window containing the renderer in question. */
} THALAMUS_SDL_RenderEvent;


/**
 * Touch finger event structure (event.tfinger.*)
 *
 * Coordinates in this event are normalized. `x` and `y` are normalized to a
 * range between 0.0f and 1.0f, relative to the window, so (0,0) is the top
 * left and (1,1) is the bottom right. Delta coordinates `dx` and `dy` are
 * normalized in the ranges of -1.0f (traversed all the way from the bottom or
 * right to all the way up or left) to 1.0f (traversed all the way from the
 * top or left to all the way down or right).
 *
 * Note that while the coordinates are _normalized_, they are not _clamped_,
 * which means in some circumstances you can get a value outside of this
 * range. For example, a renderer using logical presentation might give a
 * negative value when the touch is in the letterboxing. Some platforms might
 * report a touch outside of the window, which will also be outside of the
 * range.
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_TouchFingerEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_FINGER_DOWN, THALAMUS_SDL_EVENT_FINGER_UP, THALAMUS_SDL_EVENT_FINGER_MOTION, or THALAMUS_SDL_EVENT_FINGER_CANCELED */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_TouchID touchID; /**< The touch device id */
    THALAMUS_SDL_FingerID fingerID;
    float x;            /**< Normalized in the range 0...1 */
    float y;            /**< Normalized in the range 0...1 */
    float dx;           /**< Normalized in the range -1...1 */
    float dy;           /**< Normalized in the range -1...1 */
    float pressure;     /**< Normalized in the range 0...1 */
    THALAMUS_SDL_WindowID windowID; /**< The window underneath the finger, if any */
} THALAMUS_SDL_TouchFingerEvent;

/**
 * Pinch event structure (event.pinch.*)
 *
 * span_(x/y) and focus_(x/y) are only available for pinch gestures on mobile
 * devices
 */
typedef struct THALAMUS_SDL_PinchFingerEvent
{
    THALAMUS_SDL_EventType type; /**< ::THALAMUS_SDL_EVENT_PINCH_BEGIN or ::THALAMUS_SDL_EVENT_PINCH_UPDATE or ::THALAMUS_SDL_EVENT_PINCH_END */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    float scale;        /**< The scale change since the last THALAMUS_SDL_EVENT_PINCH_UPDATE. Scale < 1 is "zoom out". Scale > 1 is "zoom in". */
    THALAMUS_SDL_WindowID windowID; /**< The window underneath the finger, if any */
    float span_x;        /**< On mobile devices for BEGIN and UPDATE events, the average X distance between each of the pointers forming the pinch in window coordinates.  Otherwise, -1. */
    float span_y;        /**< On mobile devices for BEGIN and UPDATE events, the average Y distance between each of the pointers forming the pinch in window coordinates.  Otherwise, -1. */
    float focus_x;        /**< On mobile devices for BEGIN and UPDATE events, the X coordinate of the current gesture's focal point in window coordinates.  Otherwise, -1. */
    float focus_y;        /**< On mobile devices for BEGIN and UPDATE events, the Y coordinate of the current gesture's focal point in window coordinates.  Otherwise, -1. */
} THALAMUS_SDL_PinchFingerEvent;

/**
 * Pressure-sensitive pen proximity event structure (event.pproximity.*)
 *
 * When a pen becomes visible to the system (it is close enough to a tablet,
 * etc), SDL will send an THALAMUS_SDL_EVENT_PEN_PROXIMITY_IN event with the new pen's
 * ID. This ID is valid until the pen leaves proximity again (has been removed
 * from the tablet's area, the tablet has been unplugged, etc). If the same
 * pen reenters proximity again, it will be given a new ID.
 *
 * Note that "proximity" means "close enough for the tablet to know the tool
 * is there." The pen touching and lifting off from the tablet while not
 * leaving the area are handled by THALAMUS_SDL_EVENT_PEN_DOWN and THALAMUS_SDL_EVENT_PEN_UP.
 *
 * Not all platforms have a window associated with the pen during proximity
 * events. Some wait until motion/button/etc events to offer this info.
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_PenProximityEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_PEN_PROXIMITY_IN or THALAMUS_SDL_EVENT_PEN_PROXIMITY_OUT */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_WindowID windowID; /**< The window with pen focus, if any */
    THALAMUS_SDL_PenID which;        /**< The pen instance id */
} THALAMUS_SDL_PenProximityEvent;

/**
 * Pressure-sensitive pen motion event structure (event.pmotion.*)
 *
 * Depending on the hardware, you may get motion events when the pen is not
 * touching a tablet, for tracking a pen even when it isn't drawing. You
 * should listen for THALAMUS_SDL_EVENT_PEN_DOWN and THALAMUS_SDL_EVENT_PEN_UP events, or check
 * `pen_state & THALAMUS_SDL_PEN_INPUT_DOWN` to decide if a pen is "drawing" when
 * dealing with pen motion.
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_PenMotionEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_PEN_MOTION */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_WindowID windowID; /**< The window with pen focus, if any */
    THALAMUS_SDL_PenID which;        /**< The pen instance id */
    THALAMUS_SDL_PenInputFlags pen_state;   /**< Complete pen input state at time of event */
    float x;                /**< X coordinate, relative to window */
    float y;                /**< Y coordinate, relative to window */
} THALAMUS_SDL_PenMotionEvent;

/**
 * Pressure-sensitive pen touched event structure (event.ptouch.*)
 *
 * These events come when a pen touches a surface (a tablet, etc), or lifts
 * off from one.
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_PenTouchEvent
{
    THALAMUS_SDL_EventType type;     /**< THALAMUS_SDL_EVENT_PEN_DOWN or THALAMUS_SDL_EVENT_PEN_UP */
    Uint32 reserved;
    Uint64 timestamp;       /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_WindowID windowID;  /**< The window with pen focus, if any */
    THALAMUS_SDL_PenID which;        /**< The pen instance id */
    THALAMUS_SDL_PenInputFlags pen_state;   /**< Complete pen input state at time of event */
    float x;                /**< X coordinate, relative to window */
    float y;                /**< Y coordinate, relative to window */
    bool eraser;        /**< true if eraser end is used (not all pens support this). */
    bool down;          /**< true if the pen is touching or false if the pen is lifted off */
} THALAMUS_SDL_PenTouchEvent;

/**
 * Pressure-sensitive pen button event structure (event.pbutton.*)
 *
 * This is for buttons on the pen itself that the user might click. The pen
 * itself pressing down to draw triggers a THALAMUS_SDL_EVENT_PEN_DOWN event instead.
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_PenButtonEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_PEN_BUTTON_DOWN or THALAMUS_SDL_EVENT_PEN_BUTTON_UP */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_WindowID windowID; /**< The window with mouse focus, if any */
    THALAMUS_SDL_PenID which;        /**< The pen instance id */
    THALAMUS_SDL_PenInputFlags pen_state;   /**< Complete pen input state at time of event */
    float x;                /**< X coordinate, relative to window */
    float y;                /**< Y coordinate, relative to window */
    Uint8 button;       /**< The pen button index (first button is 1). */
    bool down;      /**< true if the button is pressed */
} THALAMUS_SDL_PenButtonEvent;

/**
 * Pressure-sensitive pen pressure / angle event structure (event.paxis.*)
 *
 * You might get some of these events even if the pen isn't touching the
 * tablet.
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_PenAxisEvent
{
    THALAMUS_SDL_EventType type;     /**< THALAMUS_SDL_EVENT_PEN_AXIS */
    Uint32 reserved;
    Uint64 timestamp;       /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_WindowID windowID;  /**< The window with pen focus, if any */
    THALAMUS_SDL_PenID which;        /**< The pen instance id */
    THALAMUS_SDL_PenInputFlags pen_state;   /**< Complete pen input state at time of event */
    float x;                /**< X coordinate, relative to window */
    float y;                /**< Y coordinate, relative to window */
    THALAMUS_SDL_PenAxis axis;       /**< Axis that has changed */
    float value;            /**< New value of axis */
} THALAMUS_SDL_PenAxisEvent;

/**
 * An event used to drop text or request a file open by the system
 * (event.drop.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_DropEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_DROP_BEGIN or THALAMUS_SDL_EVENT_DROP_FILE or THALAMUS_SDL_EVENT_DROP_TEXT or THALAMUS_SDL_EVENT_DROP_COMPLETE or THALAMUS_SDL_EVENT_DROP_POSITION */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_WindowID windowID;    /**< The window that was dropped on, if any */
    float x;            /**< X coordinate, relative to window (not on begin) */
    float y;            /**< Y coordinate, relative to window (not on begin) */
    const char *source; /**< The source app that sent this drop event, or NULL if that isn't available */
    const char *data;   /**< The text for THALAMUS_SDL_EVENT_DROP_TEXT and the file name for THALAMUS_SDL_EVENT_DROP_FILE, NULL for other events */
} THALAMUS_SDL_DropEvent;

/**
 * An event triggered when the clipboard contents have changed
 * (event.clipboard.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_ClipboardEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_CLIPBOARD_UPDATE */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    bool owner;         /**< are we owning the clipboard (internal update) */
    Sint32 num_mime_types;   /**< number of mime types */
    const char **mime_types; /**< current mime types */
} THALAMUS_SDL_ClipboardEvent;

/**
 * Sensor event structure (event.sensor.*)
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_SensorEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_SENSOR_UPDATE */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_SensorID which; /**< The instance ID of the sensor */
    float data[6];      /**< Up to 6 values from the sensor - additional values can be queried using THALAMUS_SDL_GetSensorData() */
    Uint64 sensor_timestamp; /**< The timestamp of the sensor reading in nanoseconds, not necessarily synchronized with the system clock */
} THALAMUS_SDL_SensorEvent;

/**
 * The "quit requested" event
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_QuitEvent
{
    THALAMUS_SDL_EventType type; /**< THALAMUS_SDL_EVENT_QUIT */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
} THALAMUS_SDL_QuitEvent;

/**
 * A user-defined event type (event.user.*)
 *
 * This event is unique; it is never created by SDL, but only by the
 * application. The event can be pushed onto the event queue using
 * THALAMUS_SDL_PushEvent(). The contents of the structure members are completely up to
 * the programmer; the only requirement is that '''type''' is a value obtained
 * from THALAMUS_SDL_RegisterEvents().
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef struct THALAMUS_SDL_UserEvent
{
    Uint32 type;        /**< THALAMUS_SDL_EVENT_USER through THALAMUS_SDL_EVENT_LAST, Uint32 because these are not in the THALAMUS_SDL_EventType enumeration */
    Uint32 reserved;
    Uint64 timestamp;   /**< In nanoseconds, populated using THALAMUS_SDL_GetTicksNS() */
    THALAMUS_SDL_WindowID windowID; /**< The associated window if any */
    Sint32 code;        /**< User defined event code */
    void *data1;        /**< User defined data pointer */
    void *data2;        /**< User defined data pointer */
} THALAMUS_SDL_UserEvent;


/**
 * The structure for all events in SDL.
 *
 * The THALAMUS_SDL_Event structure is the core of all event handling in SDL. THALAMUS_SDL_Event
 * is a union of all event structures used in SDL.
 *
 * \since This struct is available since SDL 3.2.0.
 */
typedef union THALAMUS_SDL_Event
{
    Uint32 type;                            /**< Event type, shared with all events, Uint32 to cover user events which are not in the THALAMUS_SDL_EventType enumeration */
    THALAMUS_SDL_CommonEvent common;                 /**< Common event data */
    THALAMUS_SDL_DisplayEvent display;               /**< Display event data */
    THALAMUS_SDL_WindowEvent window;                 /**< Window event data */
    THALAMUS_SDL_KeyboardDeviceEvent kdevice;        /**< Keyboard device change event data */
    THALAMUS_SDL_KeyboardEvent key;                  /**< Keyboard event data */
    THALAMUS_SDL_TextEditingEvent edit;              /**< Text editing event data */
    THALAMUS_SDL_TextEditingCandidatesEvent edit_candidates; /**< Text editing candidates event data */
    THALAMUS_SDL_TextInputEvent text;                /**< Text input event data */
    THALAMUS_SDL_MouseDeviceEvent mdevice;           /**< Mouse device change event data */
    THALAMUS_SDL_MouseMotionEvent motion;            /**< Mouse motion event data */
    THALAMUS_SDL_MouseButtonEvent button;            /**< Mouse button event data */
    THALAMUS_SDL_MouseWheelEvent wheel;              /**< Mouse wheel event data */
    THALAMUS_SDL_JoyDeviceEvent jdevice;             /**< Joystick device change event data */
    THALAMUS_SDL_JoyAxisEvent jaxis;                 /**< Joystick axis event data */
    THALAMUS_SDL_JoyBallEvent jball;                 /**< Joystick ball event data */
    THALAMUS_SDL_JoyHatEvent jhat;                   /**< Joystick hat event data */
    THALAMUS_SDL_JoyButtonEvent jbutton;             /**< Joystick button event data */
    THALAMUS_SDL_JoyBatteryEvent jbattery;           /**< Joystick battery event data */
    THALAMUS_SDL_GamepadDeviceEvent gdevice;         /**< Gamepad device event data */
    THALAMUS_SDL_GamepadAxisEvent gaxis;             /**< Gamepad axis event data */
    THALAMUS_SDL_GamepadButtonEvent gbutton;         /**< Gamepad button event data */
    THALAMUS_SDL_GamepadTouchpadEvent gtouchpad;     /**< Gamepad touchpad event data */
    THALAMUS_SDL_GamepadSensorEvent gsensor;         /**< Gamepad sensor event data */
    THALAMUS_SDL_GamepadCapSenseEvent gcapsense;     /**< Gamepad capsense event data */
    THALAMUS_SDL_AudioDeviceEvent adevice;           /**< Audio device event data */
    THALAMUS_SDL_CameraDeviceEvent cdevice;          /**< Camera device event data */
    THALAMUS_SDL_SensorEvent sensor;                 /**< Sensor event data */
    THALAMUS_SDL_QuitEvent quit;                     /**< Quit request event data */
    THALAMUS_SDL_UserEvent user;                     /**< Custom event data */
    THALAMUS_SDL_TouchFingerEvent tfinger;           /**< Touch finger event data */
    THALAMUS_SDL_PinchFingerEvent pinch;             /**< Pinch event data */
    THALAMUS_SDL_PenProximityEvent pproximity;       /**< Pen proximity event data */
    THALAMUS_SDL_PenTouchEvent ptouch;               /**< Pen tip touching event data */
    THALAMUS_SDL_PenMotionEvent pmotion;             /**< Pen motion event data */
    THALAMUS_SDL_PenButtonEvent pbutton;             /**< Pen button event data */
    THALAMUS_SDL_PenAxisEvent paxis;                 /**< Pen axis event data */
    THALAMUS_SDL_RenderEvent render;                 /**< Render event data */
    THALAMUS_SDL_DropEvent drop;                     /**< Drag and drop event data */
    THALAMUS_SDL_ClipboardEvent clipboard;           /**< Clipboard event data */
    THALAMUS_SDL_NotificationEvent notification;     /**< Notification event data */

    /* This is necessary for ABI compatibility between Visual C++ and GCC.
       Visual C++ will respect the push pack pragma and use 52 bytes (size of
       THALAMUS_SDL_TextEditingEvent, the largest structure for 32-bit and 64-bit
       architectures) for this union, and GCC will use the alignment of the
       largest datatype within the union, which is 8 bytes on 64-bit
       architectures.

       So... we'll add padding to force the size to be the same for both.

       On architectures where pointers are 16 bytes, this needs rounding up to
       the next multiple of 16, 64, and on architectures where pointers are
       even larger the size of THALAMUS_SDL_UserEvent will dominate as being 3 pointers.
    */
    Uint8 padding[128];
} THALAMUS_SDL_Event;

#ifdef __cplusplus
}
#endif
