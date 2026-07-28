/* Compiles the vendored TJpgDec (configured for grayscale output in
 * third_party/tjpgd/tjpgdcnf.h). TJpgDec works entirely inside a
 * caller-provided workspace — no allocation. */
#include "../../third_party/tjpgd/tjpgd.c"
