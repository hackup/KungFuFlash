/*
 * Copyright (c) 2019 Kim Jørgensen
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
#include "d64_reader.c"

static const char *d64_types[6] = {"DEL", "SEQ", "PRG", "USR", "REL", "???"};

static void sprint_u16_left(char *buffer, uint16_t val)
{
    uint16_t div = 10000;
    bool found = false;

    uint8_t ii = 0;
    for(uint8_t i=0; i<5; i++)
    {
        uint8_t d = (val / div) % 10;
        if (d || found || i==4)
        {
            found = true;
            buffer[ii++] = '0' + d;
        }
        else
        {
            buffer[4-i] = ' ';
        }

        div /= 10;
    }
}

static void d64_sanitize_name(char *dest, const char *src, uint8_t size)
{
    for(uint8_t i=0; i<size; i++)
    {
        char c = *src;
        if (c)
        {
            src++;
            if (c == '\r' || c == '\n')
            {
                c = '_';
            }
            *dest++ = c;
        }
        else
        {
            *dest++ = ' ';
        }
    }
}

static void d64_format_diskname(char *buffer, char *diskname)
{
    // Star
    sprint(scratch_buf, " *     ");
    buffer += 7;

    // Diskname
    const uint8_t diskname_len = (ELEMENT_LENGTH-7)-5;
    d64_sanitize_name(buffer, diskname, diskname_len);
    buffer += diskname_len;

    // Entry type
    sprint(buffer, " --- ");
}

static void d64_format_entry(char *buffer, D64_DIR_ENTRY *entry)
{
    // Blocks
    *buffer++ = ' ';
    sprint_u16_left(buffer, entry->blocks);
    buffer += 5;
    *buffer++ = ' ';

    // Filename
    const uint8_t filename_len = (ELEMENT_LENGTH-7)-5;
    d64_sanitize_name(buffer, entry->filename, filename_len);
    buffer += filename_len;

    // Entry type
    if(!(entry->type & 0x80))
    {
        *buffer++ = '*';   // to display really deleted files as "*DEL", $80 as " DEL"
    }
    else
    {
        *buffer++ = ' ';
    }

    uint8_t type = entry->type & 7;
    if (type > 4) type = 5;
    strcpy(buffer, d64_types[type]);
    buffer += 3;

    if(entry->type & 0x40)
    {
        *buffer++ = '<';
    }
    else
    {
        *buffer++ = ' ';
    }
}

static uint8_t d64_send_page(D64_STATE *state, uint8_t selected_element)
{
    uint8_t element;
    for (element=0; element<MAX_ELEMENTS_PAGE; element++)
    {
        if (element == 0 && state->page == 0)
        {
            sprint(scratch_buf, " ..                               DIR ");
        }
        else if (element == 1 && state->page == 0)
        {
            d64_format_diskname(scratch_buf, state->d64.diskname);
        }
        else
        {
            if (!d64_read_dir(&state->file, &state->d64))
            {
                state->d64.diskname[0] = 0;
                state->dir_end = true;
                send_page_end();
                break;
            }

            d64_format_entry(scratch_buf, state->d64.entry);
        }

        if (element == selected_element)
        {
            scratch_buf[0] = 0xa0;  // Non-breaking space
        }

        c64_send_data(scratch_buf, ELEMENT_LENGTH);
    }

    return element;
}

static bool d64_skip_to_page(D64_STATE *state, uint8_t page)
{
    bool page_found = true;
    state->page = page;

    uint8_t skip = state->page * MAX_ELEMENTS_PAGE;
    for (uint8_t i=2; i<skip; i++)
    {
        if (!d64_read_dir(&state->file, &state->d64))
        {
            page_found = false;
            break;
        }
    }

    if (!page_found)
    {
        d64_open(&state->file, &state->d64);
        state->page = 0;
    }

    return page_found;
}

static void d64_dir(D64_STATE *state)
{
    c64_send_reply(REPLY_READ_DIR);

    d64_open(&state->file, &state->d64);
    format_path(scratch_buf, true);
    c64_send_data(scratch_buf, DIR_NAME_LENGTH);
    state->dir_end = false;

    // Search for last selected element
    uint8_t selected_element;
    if (dat_file.prg.element == 0xff)
    {
        state->page = 0;
        dat_file.prg.element = 0;
        selected_element = MAX_ELEMENTS_PAGE;
    }
    else
    {
        selected_element = dat_file.prg.element % MAX_ELEMENTS_PAGE;
        uint8_t page = dat_file.prg.element / MAX_ELEMENTS_PAGE;

        if (!d64_skip_to_page(state, page))
        {
            dat_file.prg.element = 0;
            selected_element = MAX_ELEMENTS_PAGE;
        }
    }

    d64_send_page(state, selected_element);
}

static void d64_dir_up(D64_STATE *state, bool root)
{
    dat_file.prg.element = 0xff;   // Do not auto open D64 again
    menu_state = &sd_state.menu;
    if (root)
    {
        menu_state->dir_up(menu_state, root);
    }
    else
    {
        menu_state->dir(menu_state);
    }
}

static void d64_next_page(D64_STATE *state)
{
    c64_send_reply(REPLY_READ_DIR_PAGE);

    if (!state->dir_end)
    {
        state->page++;
        if (!d64_send_page(state, MAX_ELEMENTS_PAGE))
        {
            state->page--;
        }
    }
    else
    {
        send_page_end();
    }
}

static void d64_prev_page(D64_STATE *state)
{
    c64_send_reply(REPLY_READ_DIR_PAGE);

    if (state->page)
    {
        d64_open(&state->file, &state->d64);
        state->dir_end = false;

        d64_skip_to_page(state, state->page-1);
        d64_send_page(state, MAX_ELEMENTS_PAGE);
    }
    else
    {
        send_page_end();
    }
}

static bool d64_select(D64_STATE *state, uint8_t element)
{
    if (state->page == 0)
    {
        if (element == 0)
        {
            d64_dir_up(state, false);
            return false;
        }
        else if (element == 1)
        {
            element = 0xff; // Find first PRG
        }
    }
    else
    {
        element += state->page * MAX_ELEMENTS_PAGE;
    }

    d64_rewind(&state->d64);
    for (uint8_t i=2; i<=element; i++)
    {
        if (!d64_read_dir(&state->file, &state->d64))
        {
            state->d64.entry->filename[0] = 0;
            break;
        }

        if (element == 0xff && d64_is_valid_prg(&state->d64))
        {
            element = 1;
            break;
        }
    }

    if (!state->d64.entry->filename[0])
    {
        // File not not found
        if (element == 1 || element == 0xff)
        {
            dat_file.prg.element = 1;
            c64_send_exit_menu();

            sprint(scratch_buf, "No PRG files was found on disk\r\n\r\n%s", dat_file.file);
            c64_send_warning(scratch_buf);
            c64_send_reset_to_menu();
        }
        else
        {
            dat_file.prg.element = 0;
            dat_file.prg.name[0] = 0;
            d64_dir(state);
        }

        return false;
    }

    dat_file.prg.element = element;
    d64_sanitize_name(dat_file.prg.name, state->d64.entry->filename, 16);
    dat_file.prg.name[16] = 0;

    dat_file.prg.size = d64_read_prg(&state->file, &state->d64, dat_buffer, sizeof(dat_buffer));
    if (!prg_size_valid(dat_file.prg.size))
    {
        handle_unsupported(dat_file.prg.name);
        return false;
    }

    c64_send_exit_menu();
    dat_file.boot_type = DAT_PRG;
    return true;
}

static MENU_STATE * d64_menu_init(FILINFO *info)
{
    if (!d64_state.menu.dir)
    {
        d64_state.menu.dir = (void (*)(MENU_STATE *))d64_dir;
        d64_state.menu.dir_up = (void (*)(MENU_STATE *, bool))d64_dir_up;
        d64_state.menu.prev_page = (void (*)(MENU_STATE *))d64_prev_page;
        d64_state.menu.next_page = (void (*)(MENU_STATE *))d64_next_page;
        d64_state.menu.select = (bool (*)(MENU_STATE *, uint8_t))d64_select;
    }

    if (!file_open(&d64_state.file, info->fname, FA_READ))
    {
        handle_failed_to_read_sd();
    }

    return &d64_state.menu;
}
