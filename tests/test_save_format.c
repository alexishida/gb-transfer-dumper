#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "save_format.h"

int main(void)
{
    const uint64_t crystal_ram_size = 32u * 1024u;

    assert(save_format_size_is_compatible(
        crystal_ram_size, crystal_ram_size, false));
    assert(save_format_size_is_compatible(
        crystal_ram_size, crystal_ram_size, true));

    assert(save_format_size_is_compatible(
        crystal_ram_size + 44u, crystal_ram_size, true));
    assert(save_format_size_is_compatible(
        crystal_ram_size + 48u, crystal_ram_size, true));

    assert(!save_format_size_is_compatible(
        crystal_ram_size + 44u, crystal_ram_size, false));
    assert(!save_format_size_is_compatible(
        crystal_ram_size + 48u, crystal_ram_size, false));
    assert(!save_format_size_is_compatible(
        crystal_ram_size + 1u, crystal_ram_size, true));
    assert(!save_format_size_is_compatible(
        crystal_ram_size + 64u, crystal_ram_size, true));
    assert(!save_format_size_is_compatible(
        crystal_ram_size - 1u, crystal_ram_size, true));

    puts("save format tests: OK");
    return 0;
}
