/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>

/** Link every public function and return the canonical manifest size. */
size_t api_symbols_count(void);
void api_symbols_link_all_public_functions(void);
