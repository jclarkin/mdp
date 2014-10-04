/*
 * Functions necessary to parse a file and transform its content into
 * a deck of slides containing lines. All based on markdown formating
 * rules.
 * Copyright (C) 2014 Michael Goehler
 *
 * This file is part of mdp.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"

deck_t *markdown_load(FILE *input) {

    int c = 0;    // char
    int i = 0;    // increment
    int l = 0;    // line length
    int hc = 0;   // header count
    int lc = 0;   // line count
    int sc = 1;   // slide count
    int bits = 0; // markdown bits

    deck_t *deck = new_deck();
    slide_t *slide = deck->slide;
    line_t *line = NULL;
    line_t *tmp = NULL;
    cstring_t *text = cstring_init();

    while ((c = fgetc(input)) != EOF) {
        if (ferror(input)) {
            fprintf(stderr, "markdown_load() failed to read input: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if(c == '\n') {

            // markdown analyse
            bits = markdown_analyse(text);

            // if first line in file is markdown hr
            if(!line && CHECK_BIT(bits, IS_HR)) {

                // clear text
                (text->reset)(text);

            // if text is markdown hr
            } else if(CHECK_BIT(bits, IS_HR) &&
                      CHECK_BIT(line->bits, IS_EMPTY)) {

                slide->lines = lc;

                // clear text
                (text->reset)(text);
                l = 0;

                // create next slide
                slide = next_slide(slide);
                sc++;

            } else {

                // if slide ! has line
                if(!slide->line) {

                    // create new line
                    line = new_line();
                    slide->line = line;
                    lc = 1;

                } else {

                    // create next line
                    line = next_line(line);
                    lc++;

                }

                // add text to line
                line->text = text;

                // add bits to line
                line->bits = bits;

                // add length to line
                line->length = l;

                // calc offset
                line->offset = next_nonblank(text, 0);

                // new text
                text = cstring_init();
                l = 0;
            }

        } else if(c == '\t') {

            // expand tab to spaces
            for (i = 0;  i < EXPAND_TABS;  i++) {
                (text->expand)(text, ' ');
                l++;
            }

        } else if(c == '\\') {

            // add char to line
            (text->expand)(text, c);
            l++;

            // if !IS_CODE add next char to line
            // and do not increase line count
            if(next_nonblank(text, 0) < CODE_INDENT) {

                c = fgetc(input);
                (text->expand)(text, c);

                if(is_utf8(c)) {

                    // if utf-8 char > 1 byte add remaing to line
                    for(i = 0; i < length_utf8(c) - 1; i++) {
                        c = fgetc(input);
                        (text->expand)(text, c);
                    }
                }

            }

        } else if(isprint(c) || isspace((unsigned char) c)) {

            // add char to line
            (text->expand)(text, c);
            l++;

        } else if(is_utf8(c)) {

            // add char to line
            (text->expand)(text, c);

            // if utf-8 char > 1 byte add remaing to line
            for(i = 0; i < length_utf8(c) - 1; i++) {
                c = fgetc(input);
                (text->expand)(text, c);
            }

            l++;
        }
    }

    slide->lines = lc;
    deck->slides = sc;

    // detect header
    line = deck->slide->line;
    if(line && line->text->size > 0 && line->text->text[0] == '%') {

        // assign header to deck
        deck->header = line;

        // find first non-header line
        while(line->text->size > 0 && line->text->text[0] == '%') {
            hc++;
            line = line->next;
        }

        // split linked list
        line->prev->next = NULL;
        line->prev = NULL;

        // remove header lines from slide
        deck->slide->line = line;

        // adjust counts
        deck->headers += hc;
        deck->slide->lines -= hc;
    }

    slide = deck->slide;
    while(slide) {
        line = slide->line;
        while(line) {
            if((CHECK_BIT(line->bits, IS_H1) ||
                CHECK_BIT(line->bits, IS_H2)) &&
               CHECK_BIT(line->bits, IS_EMPTY) &&
               line->prev &&
               !CHECK_BIT(line->prev->bits, IS_EMPTY)) {
                // combine underlined H1/H2 in single line

                // remove line from linked list
                line->prev->next = line->next;
                if(line->next)
                    line->next->prev = line->prev;

                // set bits on previous line
                if(CHECK_BIT(line->bits, IS_H1)) {
                    SET_BIT(line->prev->bits, IS_H1);
                } else {
                    SET_BIT(line->prev->bits, IS_H2);
                }

                // adjust line count
                slide->lines -= 1;

                // maintain loop condition
                tmp = line;
                line = line->prev;

                // delete line
                (tmp->text->delete)(tmp->text);
                free(tmp);
            } else if(CHECK_BIT(line->bits, IS_UNORDERED_LIST_3)) {
                tmp = line->next;
                line_t *list_last_level_3 = line;

                while(tmp &&
                      CHECK_BIT(tmp->bits, IS_UNORDERED_LIST_3)) {
                    if(CHECK_BIT(tmp->bits, IS_UNORDERED_LIST_3)) {
                        list_last_level_3 = tmp;
                    }
                    tmp = tmp->next;
                }

                for(tmp = line; tmp != list_last_level_3; tmp = tmp->next) {
                    SET_BIT(tmp->bits, IS_UNORDERED_LIST_3);
                }
            } else if(CHECK_BIT(line->bits, IS_UNORDERED_LIST_2)) {
                tmp = line->next;
                line_t *list_last_level_2 = line;

                while(tmp &&
                      (CHECK_BIT(tmp->bits, IS_UNORDERED_LIST_2) ||
                       CHECK_BIT(tmp->bits, IS_UNORDERED_LIST_3))) {
                    if(CHECK_BIT(tmp->bits, IS_UNORDERED_LIST_2)) {
                        list_last_level_2 = tmp;
                    }
                    tmp = tmp->next;
                }

                for(tmp = line; tmp != list_last_level_2; tmp = tmp->next) {
                    SET_BIT(tmp->bits, IS_UNORDERED_LIST_2);
                }
            } else if(CHECK_BIT(line->bits, IS_UNORDERED_LIST_1)) {
                tmp = line->next;
                line_t *list_last_level_1 = line;

                while(tmp &&
                      (CHECK_BIT(tmp->bits, IS_UNORDERED_LIST_1) ||
                       CHECK_BIT(tmp->bits, IS_UNORDERED_LIST_2) ||
                       CHECK_BIT(tmp->bits, IS_UNORDERED_LIST_3))) {
                    if(CHECK_BIT(tmp->bits, IS_UNORDERED_LIST_1)) {
                        list_last_level_1 = tmp;
                    }
                    tmp = tmp->next;
                }

                for(tmp = line; tmp != list_last_level_1; tmp = tmp->next) {
                    SET_BIT(tmp->bits, IS_UNORDERED_LIST_1);
                }
            }

            line = line->next;
        }
        slide = slide->next;
    }

    return deck;
}

int markdown_analyse(cstring_t *text) {

    static int unordered_list_level = 0;
    static int unordered_list_level_offset[] = {-1, -1, -1, -1};

    int i = 0;      // increment
    int bits = 0;   // markdown bits
    int offset = 0; // text offset
    int eol    = 0; // end of line

    int equals = 0, hashes = 0,
        stars  = 0, minus  = 0,
        spaces = 0, other  = 0; // special character counts

    const int unordered_list_offset = unordered_list_level_offset[unordered_list_level];

    // count leading spaces
    offset = next_nonblank(text, 0);

    // strip trailing spaces
    for(eol = text->size; eol > offset && isspace((unsigned char) text->text[eol - 1]); eol--);

    // IS_UNORDERED_LIST_#
    if(text->size >= offset + 2 &&
       (text->text[offset] == '*' || text->text[offset] == '-') &&
       text->text[offset + 1] == ' ') {

        for(i = offset; i<eol; i++) {
            if(text->text[i] != '*' &&
               text->text[i] != '-' &&
               text->text[i] != ' ') {
                if(offset > unordered_list_offset + CODE_INDENT) {
                    SET_BIT(bits, IS_CODE);
                } else if(offset != unordered_list_offset) {
                    for(i = unordered_list_level; i >= 0; i--) {
                        if(unordered_list_level_offset[i] == offset) {
                            unordered_list_level = i;
                            break;
                        }
                    }
                    if(i != unordered_list_level) {
                        unordered_list_level = MIN(unordered_list_level + 1, UNORDERED_LIST_MAX_LEVEL);
                        unordered_list_level_offset[unordered_list_level] = offset;
                    }
                }

                if(unordered_list_level == 0) {
                    unordered_list_level = 1;
                    unordered_list_level_offset[1] = offset;
                }

                switch(unordered_list_level) {
                    case 1: SET_BIT(bits, IS_UNORDERED_LIST_1); break;
                    case 2: SET_BIT(bits, IS_UNORDERED_LIST_2); break;
                    case 3: SET_BIT(bits, IS_UNORDERED_LIST_3); break;
                    default: break;
                }

                break;
            }
        }
    }

    if(!CHECK_BIT(bits, IS_UNORDERED_LIST_1) &&
       !CHECK_BIT(bits, IS_UNORDERED_LIST_2) &&
       !CHECK_BIT(bits, IS_UNORDERED_LIST_3)) {

        unordered_list_level = 0;

        // IS_CODE
        if(offset >= CODE_INDENT) {
            SET_BIT(bits, IS_CODE);

        } else {

            for(i = offset; i < eol; i++) {

                if(text->text[i] == ' ') {
                    spaces++;

                } else {
                    switch(text->text[i]) {
                        case '=': equals++;  break;
                        case '#': hashes++;  break;
                        case '*': stars++;   break;
                        case '-': minus++;   break;
                        case '\\': other++; i++; break;
                        default:  other++;   break;
                    }
                }
            }

            // IS_H1
            if((equals > 0 &&
                hashes + stars + minus + spaces + other == 0) ||
               (text &&
                text->text &&
                text->text[offset] == '#' &&
                text->text[offset+1] != '#')) {

                SET_BIT(bits, IS_H1);
            }

            // IS_H2
            if((minus > 0 &&
                equals + hashes + stars + spaces + other == 0) ||
               (text &&
                text->text &&
                text->text[offset] == '#' &&
                text->text[offset+1] == '#')) {

                SET_BIT(bits, IS_H2);
            }

            // IS_QUOTE
            if(text &&
               text->text &&
               text->text[offset] == '>') {

                SET_BIT(bits, IS_QUOTE);
            }

            // IS_HR
            if((minus >= 3 && equals + hashes + stars + other == 0) ||
               (stars >= 3 && equals + hashes + minus + other == 0)) {

                SET_BIT(bits, IS_HR);
            }

            // IS_EMPTY
            if(other == 0) {
                SET_BIT(bits, IS_EMPTY);
            }
        }
    }

    return bits;
}

void markdown_debug(deck_t *deck, int debug) {

    int sc = 0; // slide count
    int lc = 0; // line count

    int offset;
    line_t *header;

    if(debug == 1) {
        fprintf(stderr, "headers: %i\nslides: %i\n", deck->headers, deck->slides);

    } else if(debug > 1) {

        // print header to STDERR
        if(deck->header) {
            header = deck->header;
            while(header &&
                header->length > 0 &&
                header->text->text[0] == '%') {

                // skip descriptor word (e.g. %title:)
                offset = next_blank(header->text, 0) + 1;

                fprintf(stderr, "header: %s\n", &header->text->text[offset]);
                header = header->next;
            }
        }
    }

    slide_t *slide = deck->slide;
    line_t *line;

    // print slide/line count to STDERR
    while(slide) {
        sc++;

        if(debug == 1) {
            fprintf(stderr, "  slide %i: %i lines\n", sc, slide->lines);

        } else if(debug > 1) {

            // also print bits and line length
            fprintf(stderr, "  slide %i:\n", sc);
            line = slide->line;
            lc = 0;
            while(line) {
                lc++;
                fprintf(stderr, "    line %i: bits = %i, length = %i\n", lc, line->bits, line->length);
                line = line->next;
            }
        }

        slide = slide->next;
    }
}

int is_utf8(char ch) {
    return (ch & 0x80);
}

int length_utf8(char ch) {

    int i = 0; // increment

    while(is_utf8(ch)) {
        i++;
        ch <<= 1;
    }

    return i;
}

int next_nonblank(cstring_t *text, int i) {
    while ((i < text->size) && isspace((unsigned char) (text->text)[i]))
        i++;

    return i;
}

int prev_blank(cstring_t *text, int i) {
    while ((i > 0) && !isspace((unsigned char) (text->text)[i]))
        i--;

    return i;
}

int next_blank(cstring_t *text, int i) {
    while ((i < text->size) && !isspace((unsigned char) (text->text)[i]))
        i++;

    return i;
}

int next_word(cstring_t *text, int i) {
    return next_nonblank(text, next_blank(text, i));
}
