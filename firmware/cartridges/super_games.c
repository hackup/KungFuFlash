/*
 * Copyright (c) 2019-2021 Kim Jørgensen
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

static uint8_t super_games_write_disable;

/*************************************************
* C64 bus write callback
*************************************************/
static inline void super_games_write_handler(uint32_t control, uint32_t addr, uint32_t data)
{
    /* The register is reset to $00 on reset. Bits:
        4-7 Unused
        3   Register write disable
        2   Cartridge disable
        0-1 Bank select
    */
    if (!(control & C64_IO2) && !super_games_write_disable)
    {
        crt_ptr = crt_banks[data & 0x03];

        if (data & 0x04)
        {
            c64_crt_control(STATUS_LED_OFF|CRT_PORT_NONE);
        }
        else
        {
            c64_crt_control(STATUS_LED_ON|CRT_PORT_16K);
        }

        if (data & 0x08)
        {
            super_games_write_disable = 1;
        }
    }
}

C64_BUS_HANDLER_READ(super_games, crt_read_handler)
