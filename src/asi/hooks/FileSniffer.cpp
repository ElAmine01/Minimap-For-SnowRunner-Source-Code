#include "FileSniffer.h"
#include "../core/Logger.h"

namespace snowmap::hooks {

// Level detection is now handled via direct RAM read in MinimapRenderer::Draw().
bool InstallFileSniffer() {
    SM_INFO("[FileSniffer] Skipped — level ID read directly from RAM.");
    return true;
}

} // namespace snowmap::hooks
