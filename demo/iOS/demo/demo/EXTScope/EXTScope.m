
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#import "EXTScope.h"

void ext_executeCleanupBlock (__strong ext_cleanupBlock_t *block) {
    (*block)();
}

