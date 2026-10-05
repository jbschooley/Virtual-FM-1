/* felucca_core_api.h -- the functions of the copy FEL_PREFIX names (no include guard:
 * included once per copy). */
#include "felucca_core.h"

#define FEL_DECLARE(ret, name, params) ret FEL(name) params;
FELUCCA_API(FEL_DECLARE)
#undef FEL_DECLARE
