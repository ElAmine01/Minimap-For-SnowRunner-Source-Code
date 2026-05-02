#include "Globals.h"

namespace snowmap {

Globals& G()
{
    static Globals g;
    return g;
}

} // namespace snowmap
