#pragma once
#include "types.hpp"

namespace desktop {
void initialize();
void render_if_needed();
void key(u8 scan_code);
void mouse(int dx, int dy, bool left);
}
