/* Compiles the vendored libunibreak line-breaking core (UAX #14) as one
 * translation unit. Grapheme/word breaking are not vendored — FreeInkBook
 * only needs line-break opportunities. */
#include "../../third_party/libunibreak/unibreakbase.c"
#include "../../third_party/libunibreak/unibreakdef.c"
#include "../../third_party/libunibreak/eastasianwidthdef.c"
#include "../../third_party/libunibreak/emojidef.c"
#include "../../third_party/libunibreak/linebreakdata.c"
#include "../../third_party/libunibreak/linebreakdef.c"
#include "../../third_party/libunibreak/linebreak.c"
