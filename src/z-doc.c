#include "z-doc.h"

#include "scottish.h"
#include "c-string.h"

#include <assert.h>
#include <stdint.h>

#define _INVALID_COLOR 255

doc_pos_t doc_pos_create(int x, int y)
{
    doc_pos_t result;
    result.x = x;
    result.y = y;
    return result;
}

doc_pos_t doc_pos_invalid(void)
{
    return doc_pos_create(-1, -1);
}

bool doc_pos_is_valid(doc_pos_t pos)
{
    if (pos.x >= 0 && pos.y >= 0)
        return TRUE;
    return FALSE;
}

int doc_pos_compare(doc_pos_t left, doc_pos_t right)
{
    if (left.y < right.y)
        return -1;
    if (left.y > right.y)
        return 1;
    if (left.x < right.x)
        return -1;
    if (left.x > right.x)
        return 1;
    return 0;
}

doc_region_t doc_region_create(int x1, int y1, int x2, int y2)
{
    doc_region_t result;
    result.start.x = x1;
    result.start.y = y1;
    result.stop.x = x2;
    result.stop.y = y2;
    return result;
}

doc_region_t doc_region_invalid(void)
{
    doc_region_t result;
    result.start = doc_pos_invalid();
    result.stop = doc_pos_invalid();
    return result;
}

bool doc_region_is_valid(doc_region_ptr region)
{
    if (region)
    {
        if ( doc_pos_is_valid(region->start)
          && doc_pos_is_valid(region->stop)
          && doc_pos_compare(region->start, region->stop) <= 0 )
        {
            return TRUE;
        }
    }
    return FALSE;
}

bool doc_region_contains(doc_region_ptr region, doc_pos_t pos)
{
    if (doc_region_is_valid(region) && doc_pos_is_valid(pos))
    {
        if ( doc_pos_compare(region->start, pos) <= 0
          && doc_pos_compare(region->stop, pos) > 0 )
        {
            return TRUE;
        }
    }
    return FALSE;
}

int doc_region_line_count(doc_region_ptr region)
{
    int result = 0;
    if (doc_region_is_valid(region))
    {
        result = region->stop.y - region->start.y;
        if (region->stop.x > 0)
            result++;
    }
    return result;
}

static void _doc_bookmark_free(vptr pv)
{
    doc_bookmark_ptr mark = pv;
    string_free(mark->name);
    free(mark);
}

static void _doc_link_free(vptr pv)
{
    doc_link_ptr link = pv;
    string_free(link->file);
    string_free(link->topic);
    free(link);
}

#define PAGE_HEIGHT 128
#define PAGE_NUM(a) ((a)>>7)
#define PAGE_OFFSET(a) ((a)%128)

static void _normal_style(doc_style_ptr style)
{
    style->color = TERM_WHITE;
    style->right = 72;
}

static void _note_style(doc_style_ptr style)
{
    style->color = TERM_L_GREEN;
    style->left = 4;
    style->right = 60;
}

static void _title_style(doc_style_ptr style)
{
    style->color = TERM_L_BLUE;
}

static void _heading_style(doc_style_ptr style)
{
    style->color = TERM_RED;
}

static void _keyword_style(doc_style_ptr style)
{
    style->color = TERM_L_RED;
}

static void _keypress_style(doc_style_ptr style)
{
    style->color = TERM_ORANGE;
}

static void _link_style(doc_style_ptr style)
{
    style->color = TERM_L_GREEN;
}

static void _screenshot_style(doc_style_ptr style)
{
    style->right = 255;
    style->options |= DOC_STYLE_NO_WORDWRAP;
}

static void _table_style(doc_style_ptr style)
{
    style->right = 255;
    style->options |= DOC_STYLE_NO_WORDWRAP;
}

static void _selection_style(doc_style_ptr style)
{
    style->color = TERM_YELLOW;
}

static void _indent_style(doc_style_ptr style)
{
    style->indent = 2;
}

static void _wide_style(doc_style_ptr style)
{
    style->right = 255;
}

/* doc_style_f <-> void * is forbidden in ISO C ... I'm not sure
   what the correct idiom is for a table of function pointers? */
static void _add_doc_style_f(doc_ptr doc, cptr name, doc_style_f f)
{
    doc_style_f *pf = malloc(sizeof(doc_style_f));
    *pf = f;
    str_map_add(doc->styles, name, pf);
}

static doc_style_f _get_doc_style_f(doc_ptr doc, cptr name)
{
    doc_style_f *pf = str_map_find(doc->styles, name);
    if (pf)
        return *pf;
    return NULL;
}

doc_ptr doc_alloc(int width)
{
    doc_ptr     res = malloc(sizeof(doc_t));
    doc_style_t style = {0};

    res->cursor.x = 0;
    res->cursor.y = 0;
    res->selection = doc_region_invalid();
    res->width = width;
    res->pages = vec_alloc(free);
    res->styles = str_map_alloc(free);
    res->bookmarks = vec_alloc(_doc_bookmark_free);
    res->links = int_map_alloc(_doc_link_free);
    res->style_stack = vec_alloc(free);
    res->name = string_alloc();
    res->html_header = string_alloc();
    res->hotspots = vec_alloc(NULL);
    res->selected = -1;
    res->rule_row = -1;
    res->hold_width = 0;
    memset(&res->portrait, 0, sizeof(res->portrait));

    /* Default Styles */
    _add_doc_style_f(res, "normal", _normal_style);
    _add_doc_style_f(res, "note", _note_style);
    _add_doc_style_f(res, "title", _title_style);
    _add_doc_style_f(res, "heading", _heading_style);
    _add_doc_style_f(res, "keyword", _keyword_style);
    _add_doc_style_f(res, "keypress", _keypress_style);
    _add_doc_style_f(res, "link", _link_style);
    _add_doc_style_f(res, "screenshot", _screenshot_style);
    _add_doc_style_f(res, "table", _table_style);
    _add_doc_style_f(res, "selection", _selection_style);
    _add_doc_style_f(res, "indent", _indent_style);
    _add_doc_style_f(res, "wide", _wide_style);

    /* bottom of the style stack is *always* "normal" and can never be popped */
    _normal_style(&style);
    doc_push_style(res, &style);

    return res;
}

void doc_free(doc_ptr doc)
{
    if (doc)
    {
        vec_free(doc->pages);
        str_map_free(doc->styles);
        vec_free(doc->bookmarks);
        int_map_free(doc->links);
        vec_free(doc->style_stack);
        string_free(doc->name);
        string_free(doc->html_header);
        vec_free(doc->hotspots);

        free(doc);
    }
}

void doc_change_name(doc_ptr doc, cptr name)
{
    string_clear(doc->name);
    string_append_s(doc->name, name);
}

void doc_change_html_header(doc_ptr doc, cptr header)
{
    string_clear(doc->html_header);
    string_append_s(doc->html_header, header);
}

doc_pos_t doc_cursor(doc_ptr doc)
{
    return doc->cursor;
}

int doc_line_count(doc_ptr doc)
{
    doc_region_t r = doc_range_all(doc);
    return doc_region_line_count(&r);
}

int doc_width(doc_ptr doc)
{
    return doc->width;
}

/*
 * How wide the text in a document actually is, as opposed to how wide the
 * document was allocated.
 *
 * A menu asks for a generous width so nothing wraps, then uses perhaps two
 * thirds of it. Framing the allocation rather than the text drew a box with a
 * third of it standing empty, and pushed the whole thing off centre. So measure
 * the longest line that has anything on it.
 */
void doc_hold_width(doc_ptr doc, int cx)
{
    if (doc) doc->hold_width = cx;
}

int doc_content_width(doc_ptr doc)
{
    int  widest = 0;
    doc_pos_t pos;

    for (pos.y = 0; pos.y <= doc->cursor.y; pos.y++)
    {
        doc_char_ptr cell = doc_char(doc, doc_pos_create(0, pos.y));
        int          x;

        if (!cell) continue;

        for (x = 0; x < doc->width; x++, cell++)
        {
            if (cell->c && cell->c != ' ' && x + 1 > widest) widest = x + 1;
        }
    }

    /* A document that has been told its width keeps it. See doc_hold_width. */
    if (doc->hold_width > widest) widest = doc->hold_width;
    if (widest > doc->width) widest = doc->width;

    return widest;
}

doc_region_t doc_range_all(doc_ptr doc)
{
    doc_region_t result;
    result.start = doc_pos_create(0, 0);
    result.stop = doc->cursor;
    return result;
}

doc_region_t doc_range_selection(doc_ptr doc)
{
    return doc->selection;
}

doc_region_t doc_range_top(doc_ptr doc, doc_pos_t stop)
{
    doc_region_t result;
    result.start.x = 0;
    result.start.y = 0;
    result.stop = stop;
    if (doc_pos_compare(doc->cursor, result.stop) < 0)
        result.stop = doc->cursor;
    return result;
}

doc_region_t doc_range_top_lines(doc_ptr doc, int count)
{
    doc_region_t result;
    result.start.x = 0;
    result.start.y = 0;
    result.stop.x = 0;
    result.stop.y = count; /* Remember: [start, stop)! */
    if (doc_pos_compare(doc->cursor, result.stop) < 0)
        result.stop = doc->cursor;
    return result;
}

/* `count` lines starting at `top`. doc_range_top_lines is this with top 0. */
doc_region_t doc_range_lines_from(doc_ptr doc, int top, int count)
{
    doc_region_t result;

    if (top < 0) top = 0;

    result.start.x = 0;
    result.start.y = top;
    result.stop.x = 0;
    result.stop.y = top + count; /* Remember: [start, stop)! */
    if (doc_pos_compare(doc->cursor, result.stop) < 0)
        result.stop = doc->cursor;
    return result;
}

doc_region_t doc_range_bottom(doc_ptr doc, doc_pos_t start)
{
    doc_region_t result;
    if (doc_pos_compare(doc->cursor, start) < 0)
        return doc_region_invalid();
    result.start = start;
    result.stop = doc->cursor;
    return result;
}

doc_region_t doc_range_bottom_lines(doc_ptr doc, int count)
{
    doc_region_t result;
    result.start.x = 0;
    if (doc->cursor.x > 0)
        result.start.y = MAX(0, doc->cursor.y - (count - 1));
    else
        result.start.y = MAX(0, doc->cursor.y - count);
    result.stop = doc->cursor;
    return result;
}

doc_region_t doc_range_middle(doc_ptr doc, doc_pos_t start, doc_pos_t stop)
{
    doc_region_t result;
    if (doc_pos_compare(doc->cursor, start) < 0)
        return doc_region_invalid();
    result.start = start;
    result.stop = stop;
    if (doc_pos_compare(doc->cursor, result.stop) < 0)
        result.stop = doc->cursor;
    return result;
}

doc_region_t doc_range_middle_lines(doc_ptr doc, int start_line, int stop_line)
{
    doc_region_t result;
    result.start.x = 0;
    result.start.y = start_line;
    result.stop.x = doc->width;
    result.stop.y = stop_line ;

    if (doc_pos_compare(doc->cursor, result.start) < 0)
        return doc_region_invalid();
    if (doc_pos_compare(doc->cursor, result.stop) < 0)
        result.stop = doc->cursor;
    return result;
}

doc_pos_t doc_next_bookmark(doc_ptr doc, doc_pos_t pos)
{
    int i;

    for (i = 0; i < vec_length(doc->bookmarks); i++)
    {
        doc_bookmark_ptr mark = vec_get(doc->bookmarks, i);

        if (doc_pos_compare(pos, mark->pos) < 0)
            return mark->pos;
    }
    return doc_pos_invalid();
}

doc_pos_t doc_next_bookmark_char(doc_ptr doc, doc_pos_t pos, int c)
{
    int i;
    for (i = 0; i < vec_length(doc->bookmarks); i++)
    {
        doc_bookmark_ptr mark = vec_get(doc->bookmarks, i);
        cptr             name;

        assert(mark->name);
        name = string_buffer(mark->name);

        if ( doc_pos_compare(pos, mark->pos) <= 0 /* Subtle: So I can wrap and start at (0,0) *and* pick up an initial topic */
          && string_length(mark->name) > 0
          && tolower(name[0]) == tolower(c) )
        {
            return mark->pos;
        }
    }
    return doc_pos_invalid();
}

doc_pos_t doc_prev_bookmark(doc_ptr doc, doc_pos_t pos)
{
    int i;
    doc_bookmark_ptr last = NULL;

    for (i = 0; i < vec_length(doc->bookmarks); i++)
    {
        doc_bookmark_ptr mark = vec_get(doc->bookmarks, i);

        if (doc_pos_compare(mark->pos, pos) < 0)
            last = mark;
        else
            break;
    }
    if (last)
        return last->pos;
    return doc_pos_invalid();
}

doc_pos_t doc_find_bookmark(doc_ptr doc, cptr name)
{
    int i;

    for (i = 0; i < vec_length(doc->bookmarks); i++)
    {
        doc_bookmark_ptr mark = vec_get(doc->bookmarks, i);

        if (strcmp(name, string_buffer(mark->name)) == 0)
            return mark->pos;
    }
    return doc_pos_invalid();
}

static bool _line_test_str(doc_char_ptr cell, int ncell, cptr what, int nwhat)
{
    int i;

    if (ncell < nwhat)
        return FALSE;

    for (i = 0; i < nwhat; i++, cell++)
    {
        char c = cell->c ? cell->c : ' ';
        assert(i < ncell);
        if (c != what[i]) break;
    }

    if (i == nwhat)
        return TRUE;

    return FALSE;
}

static int _line_find_str(doc_char_ptr cell, int ncell, cptr what)
{
    int i;
    int nwhat = strlen(what);

    for (i = 0; i < ncell - nwhat; i++)
    {
        if (_line_test_str(cell + i, ncell - i, what, nwhat))
            return i;
    }

    return -1;
}

doc_pos_t doc_find_next(doc_ptr doc, cptr text, doc_pos_t start)
{
    int y;

    for (y = start.y; y <= doc->cursor.y; y++)
    {
        int          x = (y == start.y) ? start.x : 0;
        int          ncell = doc->width - x;
        doc_char_ptr cell = doc_char(doc, doc_pos_create(x, y));
        int          i = _line_find_str(cell, ncell, text);

        if (i >= 0)
        {
            doc->selection.start.x = x + i;
            doc->selection.start.y = y;

            doc->selection.stop.x = x + i + strlen(text);
            doc->selection.stop.y = y;

            return doc->selection.start;
        }
   }

    doc->selection = doc_region_invalid();
    return doc_pos_invalid();
}

doc_pos_t doc_find_prev(doc_ptr doc, cptr text, doc_pos_t start)
{
    int y;

    for (y = start.y; y >= 0; y--)
    {
        int          ncell = (y == start.y) ? start.x - 1 : doc->width;
        doc_char_ptr cell = doc_char(doc, doc_pos_create(0, y));
        int          i = _line_find_str(cell, ncell, text);

        if (i >= 0)
        {
            doc->selection.start.x = i;
            doc->selection.start.y = y;

            doc->selection.stop.x = i + strlen(text);
            doc->selection.stop.y = y;

            return doc->selection.start;
        }
   }

    doc->selection = doc_region_invalid();
    return doc_pos_invalid();
}

void doc_push_style(doc_ptr doc, doc_style_ptr style)
{
    doc_style_ptr copy = malloc(sizeof(doc_style_t));

    *copy = *style;
    if (copy->right > doc->width)
        copy->right = doc->width;

    vec_push(doc->style_stack, copy);
    if (doc->cursor.x < copy->left)
    {
        doc_insert_space(doc, copy->left - doc->cursor.x);
        assert(doc->cursor.x == copy->left);
    }
}

void doc_push_named_style(doc_ptr doc, cptr name)
{
    doc_style_t copy = *doc_current_style(doc);
    doc_style_f f = _get_doc_style_f(doc, name);

    if (f)
        f(&copy);

    doc_push_style(doc, &copy);
}

void doc_pop_style(doc_ptr doc)
{
    assert(vec_length(doc->style_stack) > 0);

    /* the style stack can never empty, and the bottom is always "normal" */
    if (vec_length(doc->style_stack) > 1)
    {
        doc_style_ptr style = vec_pop(doc->style_stack);
        free(style);
    }
}

doc_style_ptr doc_current_style(doc_ptr doc)
{
    int           ct = vec_length(doc->style_stack);
    doc_style_ptr style = NULL;

    if (ct > 0)
        style = vec_get(doc->style_stack, ct - 1);

    assert(style);
    return style;
}

doc_pos_t doc_newline(doc_ptr doc)
{
    doc_style_ptr style = doc_current_style(doc);

    doc->cursor.y++;
    doc->cursor.x = 0;
    if (doc->cursor.x < style->left)
    {
        doc_insert_space(doc, style->left);
        assert(doc->cursor.x == style->left);
    }
    return doc->cursor;
}

cptr doc_parse_tag(cptr pos, doc_tag_ptr tag)
{
    /* prepare to fail! */
    tag->type = DOC_TAG_NONE;
    tag->arg = NULL;
    tag->arg_size = 0;

    /* <name:arg> where name in {"color", "style", "topic", "link"} */
    if (*pos == '<')
    {
        doc_tag_t result = {0};
        cptr seek = pos + 1;
        char name[MAX_NLEN];
        int  ct = 0;
        for (;;)
        {
            if (!*seek || strchr(" <\r\n\t", *seek)) return pos;
            if (*seek == ':' || *seek == '>') break;
            name[ct++] = *seek;
            if (ct >= MAX_NLEN) return pos;
            seek++;
        }
        name[ct] = '\0';

        /* [pos,seek) is the name of the tag */
        if (strcmp(name, "color") == 0)
            result.type = DOC_TAG_COLOR;
        else if (strcmp(name, "/color") == 0)
            result.type = DOC_TAG_CLOSE_COLOR;
        else if (strcmp(name, "style") == 0)
            result.type = DOC_TAG_STYLE;
        else if (strcmp(name, "/style") == 0)
            result.type = DOC_TAG_CLOSE_STYLE;
        else if (strcmp(name, "topic") == 0)
            result.type = DOC_TAG_TOPIC;
        else if (strcmp(name, "link") == 0)
            result.type = DOC_TAG_LINK;
        else if (strcmp(name, "$") == 0 || strcmp(name, "var") == 0)
            result.type = DOC_TAG_VAR;
        else if (strcmp(name, "indent") == 0)
            result.type = DOC_TAG_INDENT;
        else if (strcmp(name, "/indent") == 0)
            result.type = DOC_TAG_CLOSE_INDENT;
        else if (strcmp(name, "tab") == 0)
            result.type = DOC_TAG_TAB;
        else
            return pos;

        if (*seek == '>')
        {
            switch(result.type)
            {
            case DOC_TAG_CLOSE_COLOR:
            case DOC_TAG_CLOSE_STYLE:
            case DOC_TAG_INDENT:
            case DOC_TAG_CLOSE_INDENT:
                seek++;
                *tag = result;
                return seek;
            default:
                return pos;
            }
        }

        assert(*seek == ':');
        seek++;
        result.arg = seek;

        ct = 0;
        for (;;)
        {
            if (!*seek || strchr("<\r\n\t", *seek)) return pos;
            if (*seek == '>') break;
            ct++;
            seek++;
        }
        result.arg_size = ct;

        assert(*seek == '>');
        seek++;

        if (!result.arg_size)
            return pos;
        else if (result.type == DOC_TAG_COLOR)
        {
            if ( result.arg_size == 1 && result.arg[0] != '*'
              && !strchr(color_char, result.arg[0]) )
            {
                return pos;
            }
        }

        *tag = result;
        return seek;
    }

    return pos;
}

cptr doc_lex(cptr pos, doc_token_ptr token)
{
    if (!*pos)
    {
        token->type = DOC_TOKEN_EOF;
        token->pos = pos;
        token->size = 0;
        return pos;
    }
    if (*pos == '\r')
        pos++;

    if (*pos == '\n')
    {
        token->type = DOC_TOKEN_NEWLINE;
        token->pos = pos++;
        token->size = 1;
        return pos;
    }
    if (*pos == ' ' || *pos == '\t')
    {
        token->type = DOC_TOKEN_WHITESPACE;
        token->pos = pos++;
        token->size = 1;
        while (*pos && (*pos == ' ' || *pos == '\t'))
        {
            token->size++;
            pos++;
        }
        return pos;
    }
    if (*pos == '<')
    {
        token->type = DOC_TOKEN_TAG;
        token->pos = pos;
        token->size = 0;
        pos = doc_parse_tag(pos, &token->tag);
        if (token->tag.type != DOC_TAG_NONE)
        {
            token->size = pos - token->pos;
            return pos;
        }
    }
    token->type = DOC_TOKEN_WORD;
    token->pos = pos++;
    token->size = 1;

    while (*pos && !strchr(" <\n", *pos))
    {
        token->size++;
        pos++;
    }
    return pos;
}

static void _doc_process_var(doc_ptr doc, cptr name)
{
    if (strcmp(name, "version") == 0)
    {
        /* Ours, not the engine's. VER_* is 7.1.x because forty-four savefile
         * compatibility checks depend on it; nobody reading a character dump
         * wants to know that. See defines.h. */
        string_ptr s = string_alloc_format("%s", ORMEQ_VER);
        if (coffee_break == SPEED_COFFEE) string_append_s(s, "<color:U> (Coffee)</color>");
        if (coffee_break == SPEED_INSTA_COFFEE) string_append_s(s, "<color:U> (Instant Coffee)</color>");
        if (thrall_mode) string_append_s(s, "<color:R> (Thrall)</color>");
        if (wacky_rooms) string_append_s(s, "<color:v> (Wacky)</color>");
        if (VERSION_IS_DEVELOPMENT)
        {
            if ((coffee_break) && (wacky_rooms) && (thrall_mode)) string_append_s(s, "<color:B> (Dev)</color>");
            else string_append_s(s, "<color:B> (Development)</color>");
        }
        if ((VER_MINOR == 0) && (VER_MAJOR != 7)) string_append_s(s, "<color:r> (Beta)</color>");
        doc_insert(doc, string_buffer(s));
        string_free(s);
    }
    else if (strlen(name) > 3 && strncmp(name, "FF_", 3) == 0)
    {
        char buf[100];
        int  f_idx;

        sprintf(buf, "%s", name + 3);
        f_idx = f_tag_to_index(buf);

        if (0 <= f_idx && f_idx < max_f_idx)
        {
            string_ptr s = string_alloc();
            feature_type *feat = &f_info[f_idx];

            string_printf(s, "<color:%c>%c</color>",
                attr_to_attr_char(feat->d_attr[F_LIT_STANDARD]),
                feat->d_char[F_LIT_STANDARD]);

            doc_insert(doc, string_buffer(s));
            string_free(s);
        }
    }
}

/* Is the cursor at the start of its line, give or take an indent? */
static bool _at_line_start(doc_ptr doc)
{
    int x;

    for (x = 0; x < doc->cursor.x; x++)
    {
        doc_char_ptr cell = doc_char(doc, doc_pos_create(x, doc->cursor.y));

        if (!cell) return FALSE;
        /* A row that has only been indented into has never been
         * written to, and an unwritten cell holds a zero rather than
         * a space. Both mean the same thing here: nothing yet. */
        if (cell->c && cell->c != ' ')
            return FALSE;
    }
    return TRUE;
}

static void _doc_process_tag(doc_ptr doc, doc_tag_ptr tag)
{
    if ( tag->type == DOC_TAG_CLOSE_COLOR
      || tag->type == DOC_TAG_CLOSE_STYLE
      || tag->type == DOC_TAG_CLOSE_INDENT )
    {
        doc_pop_style(doc);
    }
    else if (tag->type == DOC_TAG_COLOR)
    {
        assert(tag->arg);
        if (tag->arg_size == 1)
        {
            if (tag->arg[0] == '*')
                doc_pop_style(doc);
            else
            {
                doc_style_t style = *doc_current_style(doc); /* copy */
                byte new_attr = color_char_to_attr(tag->arg[0]);
                if (new_attr != _INVALID_COLOR) style.color = new_attr;
                doc_push_style(doc, &style);
            }
        }
        else
        {
            string_ptr  arg = string_copy_sn(tag->arg, tag->arg_size);
            doc_style_t style = *doc_current_style(doc); /* copy */
            doc_style_f f = _get_doc_style_f(doc, string_buffer(arg));

            if (f)
                f(&style);

            {/* We don't copy the named style, just its color. */
             /* Also, we damn well better push a style or we'll be upset
                when the good little user pops! */
                doc_style_t copy = *doc_current_style(doc);
                copy.color = style.color;
                doc_push_style(doc, &copy);
            }
            string_free(arg);
        }
    }
    else if (tag->type == DOC_TAG_INDENT)
    {
        doc_style_t style = *doc_current_style(doc);
        style.left = doc->cursor.x;
        doc_push_style(doc, &style);
    }
    else
    {
        string_ptr arg = string_copy_sn(tag->arg, tag->arg_size);

        switch (tag->type)
        {
        case DOC_TAG_STYLE:
            if (tag->arg_size == 1 && tag->arg[0] == '*')
                doc_pop_style(doc);
            else
            {/* Better silently add one if name doesn't exist ... */
                doc_style_t copy = *doc_current_style(doc);
                doc_style_f f = _get_doc_style_f(doc, string_buffer(arg));

                if (f)
                    f(&copy);
                doc_push_style(doc, &copy);
            }
            break;
        case DOC_TAG_VAR:
            _doc_process_var(doc, string_buffer(arg));
            break;
        case DOC_TAG_TAB:
        {
            int pos = atoi(string_buffer(arg)) + doc_current_style(doc)->left;
            if (pos > doc->cursor.x)
                doc_insert_space(doc, pos - doc->cursor.x);
            else
                doc_rollback(doc, doc_pos_create(pos, doc->cursor.y));
            break;
        }
        case DOC_TAG_TOPIC:
        {
            doc_bookmark_ptr mark = malloc(sizeof(doc_bookmark_t));
            mark->name = arg; /* steal ownership */
            arg = NULL;
            mark->pos = doc->cursor;
            vec_add(doc->bookmarks, mark);
            break;
        }
        case DOC_TAG_LINK:
        {
            doc_link_ptr link = malloc(sizeof(doc_link_t));
            bool         standalone = _at_line_start(doc);
            int          split = string_chr(arg, 0, '#');
            int          ch = 'a' + int_map_count(doc->links);

            if (split >= 0)
            {
                substring_t left = string_left(arg, split);
                substring_t right = string_right(arg, string_length(arg) - split - 1);

                link->file = substring_copy(&left);
                link->topic = substring_copy(&right);
            }
            else
            {
                link->file = arg; /* steal ownership */
                arg = NULL;
                link->topic = NULL;
            }
            link->location.start = doc->cursor;
            int_map_add(doc->links, ch, link);

            /*
             * A link that begins its own line is a contents entry, and can be
             * clicked. One in the middle of a sentence -- "(see below [a])" --
             * is a reference, and a hotspot claims the whole row, so making
             * that one clickable would mean clicking a paragraph of prose and
             * being carried off somewhere else.
             */
            if (standalone)
                doc_add_hotspot(doc, ch);
            {
                /* TODO: This is flawed. Here's a real world example:
                   "(see below <link:birth.txt#PrimaryStats>)."
                   Can you see the problem? We might line break after "[a]" right
                   before ").". Instead, "[a])." should be treated as the current
                   word. To fix this, we'll need a parser with a token queue
                   that we can push onto, but this raises storage issues.
                */
                string_ptr s = string_alloc_format("<style:link>[%c]</style>", ch);
                doc_insert(doc, string_buffer(s));
                string_free(s);

                link->location.stop = doc->cursor;
            }
            break;
        }
        }

        string_free(arg);
    }
}

doc_pos_t doc_insert(doc_ptr doc, cptr text)
{
    doc_token_t token;
    doc_token_t queue[10];
    int         qidx = 0;
    cptr        pos = text;
    int         i, j, cb;
    bool        nowrap = FALSE;
    doc_style_ptr style = NULL;

    for (;;)
    {
        pos = doc_lex(pos, &token);

        if (token.type == DOC_TOKEN_EOF)
            break;

        assert(pos != token.pos);

        if (token.type == DOC_TOKEN_TAG)
        {
            _doc_process_tag(doc, &token.tag);
            continue;
        }

        if (token.type == DOC_TOKEN_NEWLINE)
        {
            doc_newline(doc);
            nowrap = FALSE;
            continue;
        }

        if (token.type == DOC_TOKEN_WHITESPACE)
        {
            doc_insert_space(doc, token.size);
            continue;
        }

        assert(token.type == DOC_TOKEN_WORD);
        assert(token.size > 0);

        /* Queue Complexity is for "<color:R>difficult</color>!" which is actually a bit common! */
        qidx = 0;
        cb = token.size;
        queue[qidx++] = token;
        while (qidx < 10)
        {
            cptr peek = doc_lex(pos, &token);
            if (token.type == DOC_TOKEN_WORD)
            {
                cb += token.size;
            }
            else if ( token.type == DOC_TOKEN_TAG
                   && (token.tag.type == DOC_TAG_COLOR || token.tag.type == DOC_TAG_CLOSE_COLOR) )
            {
            }
            else /* whitespace or newline */
            {
                break;
            }
            queue[qidx++] = token;
            pos = peek;
        }

        style = doc_current_style(doc); /* be careful ... this changes on <color:_> tags! */
        if ( doc->cursor.x + cb >= style->right
          && !(style->options & DOC_STYLE_NO_WORDWRAP) )
        {
            doc_newline(doc);
            if (style->indent)
                doc_insert_space(doc, style->indent);
        }

        for (i = 0; i < qidx; i++)
        {
            doc_token_ptr current = &queue[i];

            if (current->type == DOC_TOKEN_TAG)
            {
                assert(current->tag.type == DOC_TAG_COLOR || current->tag.type == DOC_TAG_CLOSE_COLOR);
                _doc_process_tag(doc, &current->tag);
                style = doc_current_style(doc);
            }
            else if (doc->cursor.x < style->right)
            {
                doc_char_ptr cell = doc_char(doc, doc->cursor);

                assert(current->type == DOC_TOKEN_WORD);
                assert(cell);

                for (j = 0; j < current->size && !nowrap; j++)
                {
                    cell->a = style->color;
                    cell->c = current->pos[j];
                    if (cell->c == '\t')
                        cell->c = ' ';
                    if (doc->cursor.x == style->right - 1)
                    {
                        if (style->options & DOC_STYLE_NO_WORDWRAP)
                        {
                            /* nowrap is tricky ... we still need to process remaining tokens
                             * since these may change the current style. however, we need to stop
                             * trying to print, and we'll guard this for word tokens with the
                             * following flag: */
                            nowrap = TRUE;
                            break;
                        }
                        doc_newline(doc);
                        if (style->indent)
                            doc_insert_space(doc, style->indent);
                        cell = doc_char(doc, doc->cursor);
                    }
                    else
                    {
                        doc->cursor.x++;
                        cell++;
                    }
                }
            }
        }
    }
    assert(0 <= doc->cursor.x && doc->cursor.x < doc->width);
    return doc->cursor;
}

doc_pos_t doc_insert_char(doc_ptr doc, byte a, char c)
{
    doc_char_ptr cell = doc_char(doc, doc->cursor);
    doc_style_ptr style = doc_current_style(doc);

    cell->a = a;
    cell->c = c;

    if (doc->cursor.x >= style->right - 1)
    {
        if (!(style->options & DOC_STYLE_NO_WORDWRAP))
            doc_newline(doc);
    }
    else
        doc->cursor.x++;

    assert(0 <= doc->cursor.x && doc->cursor.x < doc->width);
    return doc->cursor;
}

doc_pos_t doc_insert_space(doc_ptr doc, int count)
{
    int          i;
    doc_char_ptr cell = doc_char(doc, doc->cursor);
    doc_style_ptr style = doc_current_style(doc);

    for (i = 0; i < count; i++)
    {
        cell->a = style->color;
        cell->c = ' ';
        if (doc->cursor.x >= style->right - 1)
            break;
        doc->cursor.x++;
        cell++;
    }
    assert(0 <= doc->cursor.x && doc->cursor.x < doc->width);
    return doc->cursor;
}

doc_pos_t doc_insert_text(doc_ptr doc, byte a, cptr text)
{
    doc_style_ptr style = doc_current_style(doc);
    if (style->color != a)
    {
        doc_style_t copy = *style;
        copy.color = a;
        doc_push_style(doc, &copy);
        doc_insert(doc, text);
        doc_pop_style(doc);
    }
    else
        doc_insert(doc, text);

    assert(0 <= doc->cursor.x && doc->cursor.x < doc->width);
    return doc->cursor;
}

doc_pos_t doc_printf(doc_ptr doc, const char *fmt, ...)
{
    string_ptr s = string_alloc();
    va_list vp;

    va_start(vp, fmt);
    string_vprintf(s, fmt, vp);
    va_end(vp);

    doc_insert(doc, string_buffer(s));
    string_free(s);
    return doc->cursor;
}

doc_pos_t doc_cprintf(doc_ptr doc, byte a, const char *fmt, ...)
{
    string_ptr s = string_alloc();
    va_list vp;

    va_start(vp, fmt);
    string_vprintf(s, fmt, vp);
    va_end(vp);

    doc_insert_text(doc, a, string_buffer(s));
    string_free(s);
    return doc->cursor;
}

doc_pos_t doc_insert_doc(doc_ptr dest_doc, doc_ptr src_doc, int indent)
{
    doc_pos_t src_pos = doc_pos_create(0, 0);
    doc_pos_t dest_pos;
    int       start_y;

    if (dest_doc->cursor.x > 0)
        doc_newline(dest_doc);

    dest_pos = dest_doc->cursor;
    start_y  = dest_pos.y;

    while (src_pos.y <= src_doc->cursor.y)
    {
        int first = indent < 0 ? -MAX(indent, -src_doc->width) : 0;
        int count;
        int i;

        /* A centred block can be wider than its destination. Clip both edges
         * before obtaining cell pointers; a negative indent used to write
         * before the page allocation (notably the death card at 80 columns). */
        src_pos.x = first;
        dest_pos.x = MAX(0, indent);
        count = MIN(src_doc->width - first, dest_doc->width - dest_pos.x);
        if (src_pos.y == src_doc->cursor.y)
            count = MIN(count, src_doc->cursor.x - first);

        if (count > 0)
        {
            doc_char_ptr src = doc_char(src_doc, src_pos);
            doc_char_ptr dest = doc_char(dest_doc, dest_pos);
            for (i = 0; i < count; i++)
            {
                dest->a = src->a;
                dest->c = src->c;
                dest++;
                src++;
            }
        }

        dest_pos.x = 0;
        dest_pos.y++;
        src_pos.x = 0;
        src_pos.y++;
    }

    /*
     * The picture comes with it.
     *
     * A document holds at most one portrait, recorded against the row and
     * column it was declared at. Copying the cells across and leaving the
     * portrait behind means a page composed in a block and then inserted
     * loses its picture silently -- the box is drawn, nothing fills it, and
     * nothing anywhere says why. Inserting a document should bring what is
     * in it.
     */
    if (src_doc->portrait.live && !dest_doc->portrait.live)
    {
        dest_doc->portrait      = src_doc->portrait;
        dest_doc->portrait.row += start_y;
        dest_doc->portrait.col += indent;
    }

    dest_doc->cursor = dest_pos;
    return dest_doc->cursor;
}

doc_pos_t doc_insert_cols(doc_ptr dest_doc, doc_ptr src_cols[], int col_count, int spacing)
{
    int       src_y = 0;
    int       max_src_y = 0;
    doc_pos_t dest_pos;
    int       i;

    if (dest_doc->cursor.x > 0)
        doc_newline(dest_doc);

    dest_pos = dest_doc->cursor;

    for (i = 0; i < col_count; i++)
    {
        doc_ptr src_col = src_cols[i];
        max_src_y = MAX(src_col->cursor.y, max_src_y);
    }

    while (src_y <= max_src_y)
    {
        for (i = 0; i < col_count; i++)
        {
            doc_ptr src_col = src_cols[i];
            int     count = src_col->width;

            if (count > dest_doc->width - dest_pos.x)
                count = dest_doc->width - dest_pos.x;

            if (src_y <= src_col->cursor.y && count > 0)
            {
                doc_char_ptr dest = doc_char(dest_doc, dest_pos);
                doc_char_ptr src = doc_char(src_col, doc_pos_create(0, src_y));
                int          j;

                for (j = 0; j < count; j++)
                {
                    /* Hack: Attempt to prevent trailing spaces. For some reason,
                       this is causing oook to not display character dumps properly
                       from Windows builds. Interestingly, oook displays files correctly
                       from Linux even without this hack, which is puzzling since
                       the files seem identical on both platforms.*/
                    if (i == col_count - 1 && !src->c)
                    {
                        count = j;
                        break;
                    }

                    dest->a = src->a;
                    dest->c = src->c;

                    if (!dest->c)
                        dest->c = ' ';

                    dest++;
                    src++;
                }
            }
            dest_pos.x += count;

            if (i == col_count - 1)
                break;

            /* Spacing between columns */
            count = spacing;
            if (count > dest_doc->width - dest_pos.x)
                count = dest_doc->width - dest_pos.x;

            if (count > 0)
            {
                doc_char_ptr dest = doc_char(dest_doc, dest_pos);
                int          j;
                for (j = 0; j < count; j++)
                {
                    dest->a = TERM_WHITE;
                    dest->c = ' ';
                    dest++;
                }
            }
            dest_pos.x += count;
        }

        dest_pos.x = 0;
        dest_pos.y++;

        src_y++;
    }

    dest_doc->cursor = dest_pos;
    return dest_doc->cursor;
}

doc_char_ptr doc_char(doc_ptr doc, doc_pos_t pos)
{
    int            cb = doc->width * PAGE_HEIGHT * sizeof(doc_char_t);
    int            page_num = PAGE_NUM(pos.y);
    int            offset = PAGE_OFFSET(pos.y);
    doc_char_ptr   page = NULL;

    while (page_num >= vec_length(doc->pages))
    {
        page = malloc(cb);
        memset(page, 0, cb);
        vec_add(doc->pages, page);
    }

    page = vec_get(doc->pages, page_num);

    assert(0 <= doc->cursor.x && doc->cursor.x < doc->width);
    assert(offset * doc->width + pos.x < cb);

    return page + offset * doc->width + pos.x;
}

doc_pos_t doc_read_file(doc_ptr doc, FILE *fp)
{
    string_ptr s = string_read_file(fp);
    doc_insert(doc, string_buffer(s));
    string_free(s);
    return doc->cursor;
}

static void _doc_write_text_file(doc_ptr doc, FILE *fp)
{
    doc_pos_t    pos;
    doc_char_ptr cell;

    for (pos.y = 0; pos.y <= doc->cursor.y; pos.y++)
    {
        int cx = doc->width;
        pos.x = 0;
        if (pos.y == doc->cursor.y)
            cx = doc->cursor.x;
        cell = doc_char(doc, pos);
        for (; pos.x < cx; pos.x++)
        {
            if (!cell->c) break;
            fputc(cell->c, fp);
            cell++;
        }
        fprintf(fp, "\n");
        /*fputc('\n', fp);*/
   }
}

static int _compare_links(doc_link_ptr left, doc_link_ptr right)
{
    return doc_pos_compare(left->location.start, right->location.start);
}

vec_ptr doc_get_links(doc_ptr doc)
{
    vec_ptr          links = vec_alloc(NULL);
    int_map_iter_ptr iter;

    for (iter = int_map_iter_alloc(doc->links);
            int_map_iter_is_valid(iter);
            int_map_iter_next(iter) )
    {
        doc_link_ptr link = int_map_iter_current(iter);
        vec_add(links, link);
    }
    int_map_iter_free(iter);
    vec_sort(links, (vec_cmp_f)_compare_links);
    return links;
}

static void _doc_write_html_file(doc_ptr doc, FILE *fp)
{
    doc_pos_t        pos;
    doc_char_ptr     cell;
    byte             old_a = _INVALID_COLOR;
    int              bookmark_idx = 0;
    doc_bookmark_ptr next_bookmark = NULL;
    vec_ptr          links = doc_get_links(doc);
    int              link_idx = 0;
    doc_link_ptr     next_link = NULL;

    if (bookmark_idx < vec_length(doc->bookmarks))
        next_bookmark = vec_get(doc->bookmarks, bookmark_idx);

    if (link_idx < vec_length(links))
        next_link = vec_get(links, link_idx);

    fprintf(fp, "<!DOCTYPE html>\n<html>\n");
    if (string_length(doc->html_header))
        fprintf(fp, "%s\n", string_buffer(doc->html_header));
    fprintf(fp, "<body text=\"#ffffff\" bgcolor=\"#000000\"><pre>\n");

    for (pos.y = 0; pos.y <= doc->cursor.y; pos.y++)
    {
        int cx = doc->width;
        pos.x = 0;
        if (pos.y == doc->cursor.y)
            cx = doc->cursor.x;
        cell = doc_char(doc, pos);

        if (next_bookmark && pos.y == next_bookmark->pos.y)
        {
            fprintf(fp, "<a name=\"%s\"></a>", string_buffer(next_bookmark->name));
            bookmark_idx++;
            if (bookmark_idx < vec_length(doc->bookmarks))
                next_bookmark = vec_get(doc->bookmarks, bookmark_idx);
            else
                next_bookmark = NULL;
        }

        for (; pos.x < cx; pos.x++)
        {
            char c = cell->c;
            byte a = cell->a % MAX_COLOR;

            if (next_link)
            {
                if (doc_pos_compare(next_link->location.start, pos) == 0)
                {
                    string_ptr s;
                    int        pos = string_last_chr(next_link->file, '.');

                    if (pos >= 0)
                    {
                        s = string_copy_sn(string_buffer(next_link->file), pos + 1);
                        string_append_s(s, "html");
                    }
                    else
                        s = string_copy(next_link->file);

                    fprintf(fp, "<a href=\"%s", string_buffer(s));
                    if (next_link->topic)
                        fprintf(fp, "#%s", string_buffer(next_link->topic));
                    fprintf(fp, "\">");

                    string_free(s);
                }
                if (doc_pos_compare(next_link->location.stop, pos) == 0)
                {
                    fprintf(fp, "</a>");
                    link_idx++;
                    if (link_idx < vec_length(links))
                        next_link = vec_get(links, link_idx);
                    else
                        next_link = NULL;
                }
            }

            if (!c) break;

            if (a != old_a && c != ' ')
            {
                if (old_a != _INVALID_COLOR)
                    fprintf(fp, "</font>");
                fprintf(fp,
                    "<font color=\"#%02x%02x%02x\">",
                    ormeq_color_table[a][1],
                    ormeq_color_table[a][2],
                    ormeq_color_table[a][3]
                );
                old_a = a;
            }
            switch (c)
            {
            case '&': fprintf(fp, "&amp;"); break;
            case '<': fprintf(fp, "&lt;"); break;
            case '>': fprintf(fp, "&gt;"); break;
            default:  fprintf(fp, "%c", c); break;
            }
            cell++;
        }
        fputc('\n', fp);
   }
   fprintf(fp, "</font>");
   fprintf(fp, "</pre></body></html>\n");

   vec_free(links);
}

static void _doc_write_doc_file(doc_ptr doc, FILE *fp)
{
    doc_pos_t        pos;
    doc_char_ptr     cell;
    byte             old_a = _INVALID_COLOR;

    fputs("<style:wide>", fp);
    for (pos.y = 0; pos.y <= doc->cursor.y; pos.y++)
    {
        int cx = doc->width;
        pos.x = 0;
        if (pos.y == doc->cursor.y)
            cx = doc->cursor.x;
        cell = doc_char(doc, pos);

        for (; pos.x < cx; pos.x++)
        {
            char c = cell->c;
            byte a = cell->a;

            if (!c) break;

            if (a != old_a && c != ' ')
            {
                if (old_a != _INVALID_COLOR)
                    fputs("</color>", fp);
                fprintf(fp, "<color:%c>", attr_to_attr_char(a));
                old_a = a;
            }
            fputc(c, fp);
            cell++;
        }
        fputc('\n', fp);
   }
    fputs("</style>", fp);
}

void doc_write_file(doc_ptr doc, FILE *fp, int format)
{
    switch (format)
    {
    case DOC_FORMAT_HTML:
        _doc_write_html_file(doc, fp);
        break;
    case DOC_FORMAT_DOC:
        _doc_write_doc_file(doc, fp);
        break;
    default:
        _doc_write_text_file(doc, fp);
    }
}

typedef void (*_doc_char_fn)(doc_pos_t pos, doc_char_ptr cell);
static void _doc_for_each(doc_ptr doc, doc_region_t range, _doc_char_fn f)
{
    doc_pos_t pos;

    assert(doc_region_is_valid(&range));

    for (pos.y = range.start.y; pos.y <= range.stop.y; pos.y++)
    {
        int max_x = doc->width;

        pos.x = 0;
        if (pos.y == range.start.y)
            pos.x = range.start.x;
        if (pos.y == range.stop.y)
            max_x = range.stop.x;

        if (pos.y <= doc->cursor.y)
        {
            doc_char_ptr cell = doc_char(doc, pos);
            for (; pos.x < max_x; pos.x++, cell++)
            {
                f(pos, cell);
            }
        }
    }
}
static void _doc_clear_char(doc_pos_t pos, doc_char_ptr cell)
{
    cell->c = '\0';
    cell->a = TERM_DARK;
}
void doc_rollback(doc_ptr doc, doc_pos_t pos)
{
    doc_region_t r;
    r.start = pos;
    r.stop = doc->cursor;
    _doc_for_each(doc, r, _doc_clear_char);
    doc->cursor = pos;
}

void doc_clear(doc_ptr doc)
{
    vec_clear(doc->hotspots);
    doc->portrait.live = FALSE;
    doc->rule_row = -1;
    doc_rollback(doc, doc_pos_create(0, 0));
}

/* A hotspot is one row and the key it stands for, packed into an int to save
   an allocation per menu line. Recorded against the row the cursor is on, so
   call this while writing that row. */
void doc_add_hotspot(doc_ptr doc, char key)
{
    vec_add_int(doc->hotspots, (doc->cursor.y << 8) | (byte)key);
}


/*
 * The same, for a row the caller can name but the cursor is no longer on.
 *
 * doc_insert_cols() merges sub-documents and does not carry their hotspots
 * over -- a hotspot claims a whole row of the finished page, and a column
 * does not know which row of that page it landed on. The caller does: it
 * asked for the merge. So it registers them afterwards, by number.
 */
void doc_add_hotspot_at(doc_ptr doc, int row, char key)
{
    if (row < 0) return;
    vec_add_int(doc->hotspots, (row << 8) | (byte)key);
}

void doc_add_portrait(doc_ptr doc, int collection, cptr name,
                      int cols, int rows, byte attr, char ch)
{
    if (!doc || cols <= 0 || rows <= 0) return;

    doc->portrait.live       = TRUE;
    doc->portrait.row        = doc->cursor.y;
    doc->portrait.col        = doc->cursor.x;
    doc->portrait.cols       = cols;
    doc->portrait.rows       = rows;
    doc->portrait.collection = collection;
    doc->portrait.attr       = attr;
    doc->portrait.ch         = ch;

    if (name) my_strcpy(doc->portrait.name, name, sizeof(doc->portrait.name));
    else doc->portrait.name[0] = '\0';
}

void doc_add_rule(doc_ptr doc)
{
    if (doc) doc->rule_row = doc->cursor.y;
}

int doc_hotspot_count(doc_ptr doc)
{
    return vec_length(doc->hotspots);
}

char doc_hotspot_key(doc_ptr doc, int index)
{
    if (index < 0 || index >= vec_length(doc->hotspots)) return 0;

    return (char)(vec_get_int(doc->hotspots, index) & 0xFF);
}

int doc_selected(doc_ptr doc)
{
    return doc->selected;
}

void doc_select(doc_ptr doc, int index)
{
    doc->selected = index;
}

/*
 * Paint a document as a framed page, starting at document row `top`.
 *
 * The `top` argument is what lets a page longer than the screen scroll inside
 * its own frame. Without it a long page could only be framed once, at the top,
 * and every line after the first screenful had to be painted bare -- which is
 * how the monster recall ended up as text lying across the map.
 */
/* See doc_menu_origin(). */
static doc_pos_t _menu_origin = {0, 0};

static void _sync_menu_page(doc_ptr doc, int top, bool page)
{
    rect_t mr = ui_doc_menu_rect();
    rect_t dr = mr;
    /*
     * A page owns the screen: character creation, menus before/after play,
     * and item selectors. Ordinary menus stay dialogs during play. The home
     * screen has a composition of its own -- see ui_doc_menu_rect().
     */
    rect_t pb   = page ? ui_page_text_rect() : mr;
    int    lines;

    if (top < 0) top = 0;

    /* What is left of the document below `top`. */
    lines = doc_cursor(doc).y + 1 - top;
    if (lines < 1) lines = 1;

    /*
     * A menu opens with its first row selected.
     *
     * Otherwise there is nothing on screen to say the arrows do anything, and
     * a keyboard menu whose keyboard navigation is invisible until you guess
     * at it is not much of one. It costs nothing: the letters still work, and
     * the bar simply shows where Return would go.
     */
    /*
     * No selection unless somebody asks for one.
     *
     * This used to put the bar on the first row of every menu that had
     * clickable rows. On the start screen that is right -- Continue is what
     * Return should do -- but everywhere else it was a second highlight
     * sitting on an arbitrary row, indistinguishable from the one under the
     * pointer, and reading exactly like a bar somebody forgot to rub out.
     *
     * The bar is the keyboard's cursor. It appears when the keyboard is used,
     * and a screen that wants one to begin with says so: doc_select().
     */

    /* A menu is a layer over whatever is beneath it, and the frame belongs to
     * the layer, so raise it before a single cell is drawn. */
    ui_screen_layer_push(UI_LAYER_MENU);
    ui_event_map_hidden();
    ui_event_menu_clear();

    dr.cx = doc_content_width(doc);
    dr.cy = lines;

    /* An empty document still gets a frame rather than a zero-width sliver. */
    if (dr.cx < 8) dr.cx = 8;

    /*
     * And no narrower than three fifths of the room it has been given --
     * **only while the frame is drawn round the content**, which is during
     * play. A menu is as wide as its widest line, which for a page written
     * against an eighty-column terminal is eighty columns however large the
     * screen is, and a frame that tight is a postage stamp on a big monitor.
     *
     * A full-screen page has no such problem: the ground is the whole screen
     * either way, so the text keeps its natural width and is simply centred
     * in it. Widening it there would only stretch the lines, and long lines
     * are harder to read rather than easier.
     */
    if (mr.cx > 16 && !page)
    {
        int least = mr.cx * 3 / 5;

        if (dr.cx < least) dr.cx = least;
    }

    if (mr.cx > 16 && dr.cx > mr.cx - 4) dr.cx = mr.cx - 4;

    if (page)
    {
        /* The page's own column, whatever is on it today. */
        dr.x = pb.x;

        /* ...unless the text is wider than the measure, which a stat table or
         * a knowledge listing can be. Then it is centred as before rather than
         * hanging off the right-hand side. */
        if (dr.cx > pb.cx)
            dr.x = mr.x + (mr.cx - dr.cx) / 2;

        if (dr.x < mr.x) dr.x = mr.x;
    }
    /* Centre the content in whatever room the menu has been given. The
     * document keeps its own width -- long lines are harder to read, not
     * easier -- so on a wide screen it sits in the middle rather than hugging
     * the left edge. */
    else if (dr.cx < mr.cx)
        dr.x = mr.x + (mr.cx - dr.cx) / 2;

    /* The menu owns its whole area, so clear all of it rather than just the
     * few columns the text happens to occupy. */
    Term_clear_rect(mr);

    if (page)
    {
        /*
         * **A page, not an overlay. The panel is the screen.**
         *
         * The frame used to be drawn snugly round the content, so every page
         * of character creation got a box the size of whatever happened to be
         * on it -- and the box changed shape at every keystroke as you moved
         * between them. Twelve screens, twelve letterboxes, each jumping to a
         * new size as it arrived. And because the frame started two rows down
         * to keep clear of the message line, there was a black band across the
         * top of every one of them.
         *
         * Nothing is covering a dungeon here. The screen belongs to this page,
         * so it takes all of it, and the text stands in a box that is the same
         * on every page -- see ui_page_rect(). What the player sees change
         * between pages is the words, which are the only thing that did.
         */
        int room = pb.cy;               /* rows below the top padding */

        ui_event_panel(mr.x, mr.y, mr.cx, mr.cy);

        /* Padding is what a page gives up first. A chooser long enough to need
         * the whole screen gets the whole screen rather than losing its last
         * three rows off the bottom. */
        if (dr.cy > room)
        {
            pb.y = mr.y + (mr.cy - dr.cy);
            if (pb.y < mr.y) pb.y = mr.y;
            room = mr.y + mr.cy - pb.y;
        }

        if (dr.cy > room) dr.cy = room;

        _menu_origin = doc_pos_create(dr.x, pb.y);
        doc_sync_term(doc, doc_range_lines_from(doc, top, dr.cy),
                      doc_pos_create(dr.x, pb.y));

        /* One branch for both, so a page that has to scroll says so and one
         * that fits says the opposite -- the frontend holds this until told
         * otherwise, and the page before may well have scrolled. */
        ui_event_scroll(BOOL(top > 0),
                        BOOL(top + room <= doc_cursor(doc).y));
    }
    else if (dr.cy >= mr.cy - 1)
    {
        /* Taller than the space: the text starts at the top and there is no
         * room to frame it. */
        ui_event_panel(dr.x - 1, mr.y, dr.cx + 2, mr.cy);
        _menu_origin = doc_pos_create(dr.x, mr.y);
        doc_sync_term(doc, doc_range_lines_from(doc, top, mr.cy),
                      doc_pos_create(dr.x, mr.y));

        /* Taller than the frame, so it scrolls, and the reader has no way of
         * knowing that unless we say so. */
        ui_event_scroll(BOOL(top > 0),
                        BOOL(top + mr.cy <= doc_cursor(doc).y));
    }
    else
    {
        /*
         * During play a menu really is an overlay, and keeps the frame drawn
         * round it: high on the screen, with the dungeon still readable
         * underneath and a border saying where it ends.
         *
         * Row zero belongs to messages, which repaint on their own schedule --
         * framing it meant the panel and the message system took turns
         * painting the same cells, and that showed as a band of bare canvas
         * across the top of the frame.
         */
        dr.y = mr.y + 2;
        ui_event_panel(dr.x - 2, dr.y - 1, dr.cx + 4, dr.cy + 2);

        _menu_origin = doc_pos_create(dr.x, dr.y);
        doc_sync_term(doc, doc_range_lines_from(doc, top, dr.cy),
                      doc_pos_create(dr.x, dr.y));

        /* It all fits, so there is nothing to say. Said anyway, because the
         * frontend holds this until told otherwise and the last page shown
         * may well have scrolled. */
        ui_event_scroll(FALSE, FALSE);
    }
}

void doc_sync_menu_page(doc_ptr doc, int top)
{
    _sync_menu_page(doc, top, ui_screen_phase() != UI_PHASE_WORLD && !ui_home_layout);
}

void doc_sync_selection(doc_ptr doc)
{
    _sync_menu_page(doc, 0, TRUE);
}

void doc_sync_menu(doc_ptr doc)
{
    doc_sync_menu_page(doc, 0);
}


/*
 * Where the last menu drew itself.
 *
 * For the one thing that has to reach inside a menu from outside it: the name
 * field on the birth page, which is edited in place by the ordinary string
 * prompt and so needs a terminal position rather than a document one. It used
 * a hard-coded (7, 0), which was true while these pages were painted at the
 * top left of the screen and put the cursor outside the frame the moment they
 * were not.
 */
doc_pos_t doc_menu_origin(void)
{
    return _menu_origin;
}

void doc_sync_term(doc_ptr doc, doc_region_t range, doc_pos_t term_pos)
{
    doc_pos_t pos;
    byte      selection_color = _INVALID_COLOR;

    assert(doc_region_is_valid(&range));

    if (doc_region_is_valid(&doc->selection))
    {
        doc_style_t style = *doc_current_style(doc);
        doc_style_f f = _get_doc_style_f(doc, "selection");
        if (f)
        {
            f(&style);
            selection_color = style.color;
        }
    }

    for (pos.y = range.start.y; pos.y <= range.stop.y; pos.y++)
    {
        int term_y = term_pos.y + (pos.y - range.start.y);
        int max_x = doc->width;

        pos.x = 0;
        if (pos.y == range.start.y)
            pos.x = range.start.x;
        if (pos.y == range.stop.y)
            max_x = range.stop.x;

        if (max_x > 0)
            Term_erase(term_pos.x + pos.x, term_y, doc->width - pos.x);
        if (pos.y <= doc->cursor.y)
        {
            doc_char_ptr cell = doc_char(doc, pos);
            for (; pos.x < max_x; pos.x++, cell++)
            {
                if (cell->c)
                {
                    int term_x = term_pos.x + pos.x;
                    byte         a = cell->a;

                    if ( selection_color != _INVALID_COLOR
                      && doc_region_contains(&doc->selection, pos) )
                    {
                        a = selection_color;
                    }
                    Term_putch(term_x, term_y, a, cell->c);
                }
            }
        }
    }
    /*
     * Tell the frontend which rows stand for a keypress, so it can turn a
     * click into that key. Painting a document is exactly the moment any
     * previously declared rows stop being true, hence the clear first.
     *
     * Only for a document that has rows, though, and nothing here claims the
     * screen. This is the generic draw-a-document call -- the sidebar, the
     * status bar and the message line all come through it. Raising a menu
     * layer here meant every message printed declared a menu, so the map never
     * owned the screen and zoom did nothing until something happened to lower
     * the layer again. That belongs in doc_sync_menu(), which really is a menu.
     */
    if (vec_length(doc->hotspots))
    {
        int i;

        ui_event_menu_clear();

        for (i = 0; i < vec_length(doc->hotspots); i++)
        {
            int  packed = vec_get_int(doc->hotspots, i);
            int  row    = packed >> 8;
            char key    = (char)(packed & 0xFF);

            if (row < range.start.y || row > range.stop.y) continue;

            /* The width of the *text*, not of the allocation. A menu asks
             * for a generous width and uses two thirds of it, so reporting
             * doc->width drew a highlight that ran out past the edge of the
             * frame it was supposed to be inside. */
            ui_event_menu_item(term_pos.x,
                               term_pos.y + (row - range.start.y),
                               doc_content_width(doc), key);

            if (i == doc->selected)
                ui_event_menu_select(term_pos.x,
                                     term_pos.y + (row - range.start.y),
                                     doc_content_width(doc));
        }
    }

    /*
     * The heading is the document's first row, by convention.
     *
     * A convention rather than a mark the caller sets, because every menu
     * here already opens with its name and it would be odd to write one that
     * did not. If a document ever needs to say otherwise it can grow a call
     * for it; until then this costs nothing and asks nothing of the caller.
     */
    if (range.start.y == 0)
        ui_event_menu_title(term_pos.x, term_pos.y, doc_content_width(doc));

    /*
     * The portrait, if the whole of its box is on screen.
     *
     * All or nothing on purpose. A picture with its top sheared off by the
     * edge of the page does not read as a picture that is partly scrolled --
     * it reads as a rendering fault -- and since the box sits at the top of
     * the page, losing it is the same as having scrolled past the part of the
     * page it belongs to.
     */
    /* The rule, if its row is on screen. Same translation as everything else,
     * so a page that scrolls carries it. */
    if (doc->rule_row >= range.start.y && doc->rule_row <= range.stop.y)
    {
        ui_event_rule(term_pos.x,
                      term_pos.y + (doc->rule_row - range.start.y),
                      doc_content_width(doc));
    }

    if (doc->portrait.live
     && doc->portrait.row >= range.start.y
     && doc->portrait.row + doc->portrait.rows - 1 <= range.stop.y)
    {
        ui_event_portrait(term_pos.x + doc->portrait.col,
                          term_pos.y + (doc->portrait.row - range.start.y),
                          doc->portrait.cols, doc->portrait.rows,
                          doc->portrait.collection, doc->portrait.name,
                          NULL, doc->portrait.attr, doc->portrait.ch);
    }
}

/*
 * The two green lines around a document being read.
 *
 * They used to say "[Help file 'option.txt', Line 764/1223]" at the top and
 * "[Press ESC to exit. Press ? for help]" at the foot. Between them that is
 * the name of a file, a line number, and two of the reader's ten keys.
 *
 * A reader wants to know what they are reading and how much of it is left,
 * and wants to be told about the keys that would save them scrolling. In a
 * twelve-hundred line file the useful key is '>', which jumps to the next
 * topic, and nothing anywhere said it existed.
 */
/* The first thing written on the page, which is where a title would be. */
static bool _doc_first_line(doc_ptr doc, char *buf, int cap)
{
    int y;

    for (y = 0; y < doc->cursor.y && y < 4; y++)
    {
        int used = 0;
        int x;

        for (x = 0; x < doc->width && used + 1 < cap; x++)
        {
            doc_char_ptr cell = doc_char(doc, doc_pos_create(x, y));

            if (!cell) break;
            buf[used++] = cell->c;
        }
        while (used && buf[used - 1] == ' ') used--;
        buf[used] = '\0';

        if (used) return TRUE;
    }
    return FALSE;
}

static void _doc_view_header(doc_ptr doc, cptr caption, int top,
                             int page_size, rect_t display)
{
    char  buf[128];
    cptr  name = caption;
    int   last = MAX(0, doc->cursor.y - page_size);

    /*
     * "Help file 'objects.txt'" is how the file is filed, not what it is.
     * Seventy-five of the seventy-seven help files open with a title
     * line, so the first thing written in the document is its name.
     * Nothing captures that text as it is parsed -- <style:title> only
     * sets a colour -- but it does not need to be captured to be read
     * back off the page.
     */
    if (strncmp(name, "Help file '", 11) == 0)
    {
        if (_doc_first_line(doc, buf, sizeof(buf)))
            name = buf;
        else
        {
            char *p;

            my_strcpy(buf, name + 11, sizeof(buf));
            p = strchr(buf, '\'');
            if (p) *p = '\0';
            p = strstr(buf, ".txt");
            if (p) *p = '\0';
            name = buf;
        }
    }

    c_put_str(TERM_L_GREEN, format("[%s]", name), display.y, display.x);

    /*
     * Where you are, in the terms a reader thinks in.
     *
     * Built in a local buffer rather than by nesting two format() calls:
     * format() hands back a slot in a rotating buffer, so passing one of
     * its results back into it prints the wrong thing.
     */
    if (last > 0)
    {
        char where[32];
        int  x;

        if (top == 0)         my_strcpy(where, "[the beginning]", sizeof(where));
        else if (top >= last) my_strcpy(where, "[the end]", sizeof(where));
        else sprintf(where, "[%d%% in]", (top * 100) / last);

        x = display.x + display.cx - strlen(where) - 1;
        if (x > display.x + (int)strlen(name) + 3)
            c_put_str(TERM_L_DARK, where, display.y, x);
    }
}

static void _doc_view_footer(doc_ptr doc, cptr caption, rect_t display)
{
    int y=display.y+display.cy-1;
    c_put_str(TERM_L_GREEN,"[Esc Back]  [| Export]  [? Help]",y,display.x);
    ui_event_menu_item(display.x,y,10,ESCAPE);
    ui_event_menu_item(display.x+12,y,10,'|');
    ui_event_menu_item(display.x+24,y,8,'?');
    ui_event_scroll(TRUE,TRUE);
}

static int _viewer_depth;
bool doc_viewer_active(void) { return _viewer_depth>0; }

#define _UNWIND 1
#define _OK 0
int doc_display(doc_ptr doc, cptr caption, int top)
{
    rect_t screen = {0};
    rect_t display;

    Term_get_size(&screen.cx, &screen.cy);

    /*
     * The viewer gets the same margins a page of character creation gets.
     *
     * It used the raw terminal, so every help topic was flush against all
     * four edges of the window -- and because help is written for eighty
     * columns, a third of a wide window stood empty beside it. Same page,
     * same box, and the header and footer travel with the text.
     */
    display = ui_page_text_rect();

    /* Too small to give any of it away. */
    if (display.cy < 8 || display.cx < 40) display = screen;

    return doc_display_aux(doc, caption, top, display);
}
static int _doc_display_aux(doc_ptr doc, cptr caption, int top, rect_t display,
    int *resume_top, void (*header)(rect_t))
{
    int     rc = _OK;
    char    finder_str[81];
    char    back_str[81];
    int     page_size;
    bool    done = FALSE;
    int screen_w=Term->wid,screen_h=Term->hgt;
    int sounded_top;
    bool    verify_format_hack = (strpos("Character Sheet", caption) == 1);

    ++_viewer_depth;
    sound(SOUND_UI_OPEN);
    strcpy(finder_str, "");

    page_size = display.cy - 4;

    if (top < 0)
        top = 0;
    if (top > doc->cursor.y - page_size)
        top = MAX(0, doc->cursor.y - page_size);

    /*
     * This viewer takes the whole screen, and until now it took it without
     * telling anybody. A frontend that had drawn chrome for whatever was on
     * screen a moment ago -- the frame around a menu, the row its cursor was
     * on -- had no way to know that menu was gone, and went on painting the
     * frame underneath the document. Say it, using the two events that already
     * mean exactly this.
     */
    ui_event_map_hidden();
    ui_event_menu_clear();

    /* A ground under the whole page. This said panel_none() and then drew,
     * which put the header and the first line of every help topic on bare
     * canvas -- the black band at the top. The viewer takes the screen; it
     * may as well look like it meant to. */
    ui_event_panel_screen();

    /*
     * All of the screen, not just the box the text goes in.
     *
     * This erased exactly the display rect, which was the whole screen, so
     * the distinction never came up. Now that a page keeps margins, the cells
     * outside them are ones Term still believes hold whatever was there
     * before -- and what was there before is the dungeon, which duly showed
     * through the margins of every help topic opened during play.
     */
    Term_clear_rect(ui_screen_rect());

    sounded_top = top;
    while (!done)
    {
        int cmd;
        if (top != sounded_top) sound(SOUND_UI_NAVIGATE);
        sounded_top = top;

        if(screen_w!=Term->wid || screen_h!=Term->hgt)
        {
            screen_w=Term->wid; screen_h=Term->hgt;
            display=ui_page_text_rect(); page_size=MAX(1,display.cy-4);
            top=MAX(0,MIN(top,doc->cursor.y-page_size));
            Term_clear_rect(ui_screen_rect());
        }
        ui_event_menu_clear();
        Term_erase(display.x, display.y, display.cx);
        if (header)
        {
            rect_t page=ui_page_rect();
            header(display);
            if(page.cx>=64) c_put_str(TERM_SLATE,format("[%d%% in]",100*top/MAX(1,doc->cursor.y-page_size)),page.y,page.x+page.cx-10);
        }
        else _doc_view_header(doc, caption, top, page_size, display);
        doc_sync_term(doc, doc_region_create(0, top, doc->width, top + page_size - 1), doc_pos_create(display.x, display.y + 2));
        Term_erase(display.x, display.y + display.cy - 1, display.cx);
        _doc_view_footer(doc, caption, display);
        ui_event_scroll(top>0,top<doc->cursor.y-page_size);
        /* Changing tab surfaces can invalidate the frontend's backing texture
         * even where the new page happens to contain the same characters. */
        if (header) Term_dirty();

        cmd = inkey_special(TRUE);
        if (cmd==SPECIAL_KEY_UI_REDRAW) continue;
        if (header && cmd == '\t') { sound(SOUND_UI_TAB); rc = '\t'; break; }

        /* links */
        if ('a' <= cmd && cmd <= 'z')
        {
            doc_link_ptr link = int_map_find(doc->links, cmd);
            if (link)
            {
                rc = doc_display_help_aux(string_buffer(link->file), string_buffer(link->topic), display);
                if (rc == _UNWIND)
                    done = TRUE;
                continue;
            }
        }

        /* vi like movement for roguelike keyset */
        if (rogue_like_commands)
        {
            if (cmd == 'j')
            {
                top++;
                if (top > doc->cursor.y - page_size)
                    top = MAX(0, doc->cursor.y - page_size);
                continue;
            }
            else if (cmd == 'k')
            {
                top--;
                if (top < 0) top = 0;
                continue;
            }
            else if (cmd == KTRL('F'))
            {
                top += page_size;
                if (top > doc->cursor.y - page_size)
                    top = MAX(0, doc->cursor.y - page_size);
                continue;
            }
            else if (cmd == KTRL('B'))
            {
                top -= page_size;
                if (top < 0) top = 0;
                continue;
            }
        }

        switch (cmd)
        {
        case '?':
            if (!strstr(caption, "helpinfo.txt"))
            {
                rc = doc_display_help_aux("helpinfo.txt", NULL, display);
                if (rc == _UNWIND)
                    done = TRUE;
            }
            break;
        case '!':
            if (!strstr(caption, "start.txt"))
            {
                rc = doc_display_help_aux("start.txt", NULL, display);
                if (rc == _UNWIND)
                    done = TRUE;
            }
            break;
        case ESCAPE:
            done = TRUE;
            break;
        case 'q':
        case 'Q':
            done = TRUE;
            rc = _UNWIND;
            break;
        case SKEY_TOP:
        case '7':
            top = 0;
            break;
        case SKEY_BOTTOM:
        case '1':
            top = MAX(0, doc->cursor.y - page_size);
            break;
        case SKEY_PGUP:
        case '9':
            top -= page_size;
            if (top < 0) top = 0;
            break;
        case SKEY_PGDOWN:
        case '3':
            top += page_size;
            if (top > doc->cursor.y - page_size)
                top = MAX(0, doc->cursor.y - page_size);
            break;
        case SKEY_UP:
        case '8':
            top--;
            if (top < 0) top = 0;
            break;
        case SKEY_DOWN:
        case '2':
            top++;
            if (top > doc->cursor.y - page_size)
                top = MAX(0, doc->cursor.y - page_size);
            break;
        case '>':
        {
            doc_pos_t pos = doc_next_bookmark(doc, doc_pos_create(doc->width - 1, top));
            if (doc_pos_is_valid(pos))
            {
                top = pos.y;
                if (top > doc->cursor.y - page_size)
                    top = MAX(0, doc->cursor.y - page_size);
            }
            break;
        }
        case '<':
        {
            doc_pos_t pos = doc_prev_bookmark(doc, doc_pos_create(0, top));
            if (doc_pos_is_valid(pos))
                top = pos.y;
            else
                top = 0;
            break;
        }
        case '|':
        {
            FILE *fp2;
            char buf[1024];
            char name[82];
            int  cb;
            int  format = DOC_FORMAT_TEXT;

            strcpy(name, string_buffer(doc->name));

            if (!get_string("File name: ", name, 80)) break;
            path_build(buf, sizeof(buf), SCOTTISH_DIR_USER, name);

            cb = strlen(buf);
            if (cb > 5 && strcmp(buf + cb - 5, ".html") == 0)
                format = DOC_FORMAT_HTML;
            else if (cb > 4 && strcmp(buf + cb - 4, ".htm") == 0)
                format = DOC_FORMAT_HTML;

            if ((format != DOC_FORMAT_HTML) && (verify_format_hack) && (strlen(buf)))
            {
                char nuname[1024], prompt[256];
                int i, paikka = 0;
                strcpy(nuname, buf);
                for (i = strlen(buf) - 1; ((i > 0) && (i > (int)strlen(buf) - 7)); i--)
                {
                    unsigned char testi = buf[i];
                    if (testi == '/') break;
                    if (testi == '.')
                    {
                        paikka = i + 1;
                        break;
                    }
                }
                if (paikka)
                {
                    for (i = strlen(buf) - 1; i > paikka - 2; i--)
                    {
                        nuname[i] = '\0';
                    }
                }
                strcat(nuname, ".html");
                sprintf(prompt, "Please note that the Scottish game's Ladder at angband.oook.cz only accepts HTML dumps.\n<color:y>Save dump as</color> <color:R>%s</color><color:y>? [y/n]</color>", nuname);
                if (msg_prompt(prompt, "ny", PROMPT_DEFAULT) == 'y')
                {
                    strcpy(buf, nuname);
                    format = DOC_FORMAT_HTML;
                }
            }

            fp2 = my_fopen(buf, "r");
            if (fp2)
            {
                my_fclose(fp2);
                if (!get_check("That file exists. Replace it? ")) break;
            }
            fp2 = my_fopen(buf, "w");
            if (!fp2)
            {
                msg_format("Failed to open file: %s", buf);
                break;
            }

            doc_write_file(doc, fp2, format);
            my_fclose(fp2);
            msg_format("Created file: %s", buf);
            msg_print(NULL);
            break;
        }
        case '/':
            Term_erase(display.x, display.y + display.cy - 1, display.cx);
            put_str("Find: ", display.y + display.cy - 1, display.x);
            strcpy(back_str, finder_str);
            if (askfor(finder_str, 80))
            {
                if (finder_str[0])
                {
                    doc_pos_t pos = doc->selection.stop;
                    if (!doc_pos_is_valid(pos))
                        pos = doc_pos_create(0, top);
                    pos = doc_find_next(doc, finder_str, pos);
                    if (doc_pos_is_valid(pos))
                    {
                        top = pos.y;
                        if (top > doc->cursor.y - page_size)
                            top = MAX(0, doc->cursor.y - page_size);
                    }
                }
            }
            else strcpy(finder_str, back_str);
            break;
        case '\\':
            Term_erase(display.x, display.y + display.cy - 1, display.cx);
            put_str("Find: ", display.y + display.cy - 1, display.x);
            strcpy(back_str, finder_str);
            if (askfor(finder_str, 80))
            {
                if (finder_str[0])
                {
                    doc_pos_t pos = doc->selection.start;
                    if (!doc_pos_is_valid(pos))
                        pos = doc_pos_create(doc->width, top + page_size);
                    pos = doc_find_prev(doc, finder_str, pos);
                    if (doc_pos_is_valid(pos))
                    {
                        top = pos.y;
                        if (top > doc->cursor.y - page_size)
                            top = MAX(0, doc->cursor.y - page_size);
                    }
                }
            }
            else strcpy(finder_str, back_str);
            break;
        default:
        {   /* BETA: Any unhandled keystroke will navigate to the next topic based
                     upon a comparison of the first letter. This is nice, say, for
                     viewing the Character Sheet and navigating to the various sections */
            doc_pos_t pos = doc_next_bookmark_char(doc, doc_pos_create(1, top), cmd);
            if (!doc_pos_is_valid(pos)) /* wrap */
                pos = doc_next_bookmark_char(doc, doc_pos_create(0, 0), cmd);
            if (doc_pos_is_valid(pos))
            {
                top = pos.y;
                if (top > doc->cursor.y - page_size)
                    top = MAX(0, doc->cursor.y - page_size);
            }
        }
        }
    }
    --_viewer_depth;
    if (rc != '\t') sound(SOUND_UI_CLOSE);
    if (resume_top) *resume_top = top;
    ui_event_menu_clear();
    return rc;
}

int doc_display_aux(doc_ptr doc, cptr caption, int top, rect_t display)
{
    return _doc_display_aux(doc, caption, top, display, NULL, NULL);
}

int doc_display_tab(doc_ptr doc, cptr caption, int *top, void (*header)(rect_t))
{
    return _doc_display_aux(doc, caption, *top, ui_page_text_rect(), top, header);
}

/* Modified version of doc_display_aux() to fulfill the needs of the proficiency table */
int weapon_exp_display(doc_ptr doc, cptr caption, int *top)
{
    int     rc = 0;
    bool    change_mode = FALSE;
    int     i;
    char    finder_str[81];
    char    back_str[81];
    int     page_size;
    bool    done = FALSE;
    rect_t display = {0};
    Term_get_size(&display.cx, &display.cy);

    strcpy(finder_str, "");

    page_size = display.cy - 4;

    if (*top < 0)
        *top = 0;
    if (*top > doc->cursor.y - page_size)
        *top = MAX(0, doc->cursor.y - page_size);

    for (i = 0; i < display.cy; i++)
        Term_erase(display.x, display.y + i, display.cx);

    while (!done)
    {
        int cmd;

        Term_erase(display.x, display.y, display.cx);
        c_put_str(TERM_L_GREEN, format("[%s, Line %d/%d]", caption, *top, doc->cursor.y), display.y, display.x);
        doc_sync_term(doc, doc_region_create(0, *top, doc->width, *top + page_size - 1), doc_pos_create(display.x, display.y + 2));
        Term_erase(display.x, display.y + display.cy - 1, display.cx);
        c_put_str(TERM_L_GREEN, "[Press ESC to exit. Press M to toggle mode. Press ? for help]", display.y + display.cy - 1, display.x);

        cmd = inkey_special(TRUE);

        /* vi like movement for roguelike keyset */
        if (rogue_like_commands)
        {
            if (cmd == 'j')
            {
                *top += 1;
                if (*top > doc->cursor.y - page_size)
                    *top = MAX(0, doc->cursor.y - page_size);
                continue;
            }
            else if (cmd == 'k')
            {
                *top -= 1;
                if (*top < 0) *top = 0;
                continue;
            }
            else if (cmd == KTRL('F'))
            {
                *top += page_size;
                if (*top > doc->cursor.y - page_size)
                    *top = MAX(0, doc->cursor.y - page_size);
                continue;
            }
            else if (cmd == KTRL('B'))
            {
                *top -= page_size;
                if (*top < 0) *top = 0;
                continue;
            }
        }

        switch (cmd)
        {
        case '?':
            if (!strstr(caption, "helpinfo.txt"))
            {
                rc = doc_display_help_aux("helpinfo.txt", NULL, display);
                if (rc == 1)
                    done = TRUE;
            }
            break;
        case ESCAPE:
        case 'q':
            done = TRUE;
            break;
        case SKEY_TOP:
        case '7':
            *top = 0;
            break;
        case SKEY_BOTTOM:
        case '1':
            *top = MAX(0, doc->cursor.y - page_size);
            break;
        case SKEY_PGUP:
        case '9':
            *top -= page_size;
            if (*top < 0) *top = 0;
            break;
        case SKEY_PGDOWN:
        case '3':
            *top += page_size;
            if (*top > doc->cursor.y - page_size)
                *top = MAX(0, doc->cursor.y - page_size);
            break;
        case SKEY_UP:
        case '8':
            *top -= 1;
            if (*top < 0) *top = 0;
            break;
        case SKEY_DOWN:
        case '2':
            *top += 1;
            if (*top > doc->cursor.y - page_size)
                *top = MAX(0, doc->cursor.y - page_size);
            break;
        case '>':
        {
            doc_pos_t pos = doc_next_bookmark(doc, doc_pos_create(doc->width - 1, *top));
            if (doc_pos_is_valid(pos))
            {
                *top = pos.y;
                if (*top > doc->cursor.y - page_size)
                    *top = MAX(0, doc->cursor.y - page_size);
            }
            break;
        }
        case '<':
        {
            doc_pos_t pos = doc_prev_bookmark(doc, doc_pos_create(0, *top));
            if (doc_pos_is_valid(pos))
                *top = pos.y;
            else
                *top = 0;
            break;
        }
        case '|':
        {
            FILE *fp2;
            char buf[1024];
            char name[82];
            int  cb;
            int  format = DOC_FORMAT_TEXT;

            strcpy(name, string_buffer(doc->name));

            if (!get_string("File name: ", name, 80)) break;
            path_build(buf, sizeof(buf), SCOTTISH_DIR_USER, name);
            fp2 = my_fopen(buf, "w");
            if (!fp2)
            {
                msg_format("Failed to open file: %s", buf);
                break;
            }

            cb = strlen(buf);
            if (cb > 5 && strcmp(buf + cb - 5, ".html") == 0)
                format = DOC_FORMAT_HTML;
            else if (cb > 4 && strcmp(buf + cb - 4, ".htm") == 0)
                format = DOC_FORMAT_HTML;

            doc_write_file(doc, fp2, format);
            my_fclose(fp2);
            msg_format("Created file: %s", buf);
            msg_print(NULL);
            break;
        }
        case '/':
            Term_erase(display.x, display.y + display.cy - 1, display.cx);
            put_str("Find: ", display.y + display.cy - 1, display.x);
            strcpy(back_str, finder_str);
            if (askfor(finder_str, 80))
            {
                if (finder_str[0])
                {
                    doc_pos_t pos = doc->selection.stop;
                    if (!doc_pos_is_valid(pos))
                        pos = doc_pos_create(0, *top);
                    pos = doc_find_next(doc, finder_str, pos);
                    if (doc_pos_is_valid(pos))
                    {
                        *top = pos.y;
                        if (*top > doc->cursor.y - page_size)
                            *top = MAX(0, doc->cursor.y - page_size);
                    }
                }
            }
            else strcpy(finder_str, back_str);
            break;
        case '\\':
            Term_erase(display.x, display.y + display.cy - 1, display.cx);
            put_str("Find: ", display.y + display.cy - 1, display.x);
            strcpy(back_str, finder_str);
            if (askfor(finder_str, 80))
            {
                if (finder_str[0])
                {
                    doc_pos_t pos = doc->selection.start;
                    if (!doc_pos_is_valid(pos))
                        pos = doc_pos_create(doc->width, *top + page_size);
                    pos = doc_find_prev(doc, finder_str, pos);
                    if (doc_pos_is_valid(pos))
                    {
                        *top = pos.y;
                        if (*top > doc->cursor.y - page_size)
                            *top = MAX(0, doc->cursor.y - page_size);
                    }
                }
            }
            else strcpy(finder_str, back_str);
            break;
        case 'm':
        case 'M':
            change_mode = TRUE;
            done = TRUE;
            break;
        default:
            break;
        }
    }
    return (change_mode) ? 1 : 0;
}
int doc_display_help(cptr file_name, cptr topic)
{
    /* The same page box the viewer uses for anything else. See doc_display(). */
    return doc_display_help_aux(file_name, topic, ui_page_text_rect());
}
int doc_display_help_aux(cptr file_name, cptr topic, rect_t display)
{
    int     rc = _OK;
    FILE   *fp = NULL;
    char    path[1024];
    char    caption[1024];
    doc_ptr doc = NULL;
    int     top = 0;

    /* Check for file_name#topic from a lazy client */
    if (!topic)
    {
        cptr pos = strchr(file_name, '#');
        if (pos)
        {
            string_ptr name = string_copy_sn(file_name, pos - file_name);
            int        result = doc_display_help_aux(string_buffer(name), pos + 1, display);

            string_free(name);
            return result;
        }
    }

    sprintf(caption, "Help file '%s'", file_name);
    path_build(path, sizeof(path), SCOTTISH_DIR_HELP, file_name);
    fp = my_fopen(path, "r");
    if (!fp)
    {
        cmsg_format(TERM_VIOLET, "Cannot open '%s'.", file_name);
        msg_print(NULL);
        return _OK;
    }

    doc = doc_alloc(MIN(80, display.cx));
    doc_read_file(doc, fp);
    my_fclose(fp);

    if (topic)
    {
        doc_pos_t pos = doc_find_bookmark(doc, topic);
        if (doc_pos_is_valid(pos))
            top = pos.y;
    }

    rc = doc_display_aux(doc, caption, top, display);
    doc_free(doc);
    return rc;
}
