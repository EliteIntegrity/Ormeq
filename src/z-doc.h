#ifndef INCLUDED_Z_DOC_H
#define INCLUDED_Z_DOC_H

#include "h-basic.h"
#include "rect.h"

#include "str-map.h"
#include "int-map.h"
#include "c-vec.h"
#include "c-string.h"

/* Utilities for Formatted Text Processing
   We support the ability to render rich text to a "virtual terminal",
   and then rapidly transfer a region of rendered text to the actual
   terminal for display. This is useful any time you want to support
   scrolling, wordwrapping, or color coded text (e.g. messages or
   the help system). We also support linked navigation, named styles and
   named bookmarks (called topics).

   The virtual terminal is a fixed width beast, but can grow its height
   dynamically. This is the way a normal "document" behaves: It grows downward
   as you type in new content.

   We support tags to control output and provide help features:
     <color:x> where x is one of the color codes:dwsorgbuDWvyRGBU*
     <style:name>
     <topic:name>
     <link:file#topic>
     <$:var> where var is a valid variable reference (e.g. <$:version> for news.txt)
*/

/* The Datatypes
   Key concepts are a position (for the cursor), a region (for transfers),
   a character (for contents and color), a style (for named formatting) and,
   of course, a document.

   Sample Usage:
    void test(int width)
    {
        doc_ptr doc = doc_alloc(width);

        doc_read_file(doc, stdin);
        doc_write_file(doc, stdout);

        doc_free(doc);
    }
*/

struct doc_pos_s
{
    int x;
    int y;
};
typedef struct doc_pos_s doc_pos_t;

doc_pos_t doc_pos_create(int x, int y);

doc_pos_t doc_pos_invalid(void);
bool      doc_pos_is_valid(doc_pos_t pos);
int       doc_pos_compare(doc_pos_t left, doc_pos_t right);

struct doc_region_s
{/* [start, stop) */
    doc_pos_t start;
    doc_pos_t stop;
};
typedef struct doc_region_s doc_region_t, *doc_region_ptr;

doc_region_t doc_region_create(int x1, int y1, int x2, int y2);
doc_region_t doc_region_invalid(void);
bool doc_region_is_valid(doc_region_ptr region);
bool doc_region_contains(doc_region_ptr region, doc_pos_t pos);
int  doc_region_line_count(doc_region_ptr region);

struct doc_char_s
{
    char c;
    byte a; /* attribute */
};
typedef struct doc_char_s doc_char_t, *doc_char_ptr;

struct doc_style_s
{
    byte color;
    int  left;
    int  right;
    int  indent;
    int  options;
};
typedef struct doc_style_s doc_style_t, *doc_style_ptr;
enum doc_style_options_e
{
    DOC_STYLE_NO_WORDWRAP = 0x0001,
};

typedef void (*doc_style_f)(doc_style_ptr style);

struct doc_bookmark_s
{
    string_ptr name;
    doc_pos_t  pos;
};
typedef struct doc_bookmark_s doc_bookmark_t, *doc_bookmark_ptr;

struct doc_link_s
{
    string_ptr   file;
    string_ptr   topic;
    doc_region_t location;
};
typedef struct doc_link_s doc_link_t, *doc_link_ptr;

/*
 * A picture belongs in this part of the document.
 *
 * Recorded rather than drawn, and translated to the screen at sync time along
 * with everything else, so a page that scrolls carries its portrait with it
 * and one that has scrolled past simply stops asking for it. The document
 * itself leaves the box blank; a frontend that can draw pictures fills it, and
 * one that cannot leaves the blank, which is what a terminal has always shown.
 */
struct doc_portrait_s
{
    bool live;
    int  row, col;      /* in document coordinates */
    int  cols, rows;    /* the box, in cells       */
    int  collection;    /* UI_ART_MONSTER, UI_ART_SHOPKEEPER */
    byte attr;          /* the classic glyph, to fall back to */
    char ch;
    char name[64];
};
typedef struct doc_portrait_s doc_portrait_t;

struct doc_s
{
    doc_pos_t      cursor;
    doc_region_t   selection;
    int            width;
    vec_ptr        pages;
    str_map_ptr    styles;
    vec_ptr        bookmarks;
    int_map_ptr    links;
    vec_ptr        style_stack;
    string_ptr     name;
    string_ptr     html_header;
    vec_ptr        hotspots;   /* rows that stand for a keypress; see doc_add_hotspot */
    int            selected;   /* which hotspot the keyboard is on, or -1 */
    doc_portrait_t portrait;   /* see doc_add_portrait; at most one per page */
    int            rule_row;   /* see doc_add_rule; -1 for none */
    int            hold_width; /* see doc_hold_width; 0 for none */
};
typedef struct doc_s doc_t, *doc_ptr;


/* Document API
   All text is added at the current location. The intention is that you build your
   document up front (by reading a help file, for example), and then use the
   copy_to_term() method to render to the display one screen at a time, depending
   on the current scroll position.
*/

doc_ptr       doc_alloc(int width);
void          doc_free(doc_ptr doc);

doc_pos_t     doc_cursor(doc_ptr doc);

doc_region_t  doc_range_all(doc_ptr doc);
doc_region_t  doc_range_selection(doc_ptr doc);
doc_region_t  doc_range_top(doc_ptr doc, doc_pos_t stop);
doc_region_t  doc_range_top_lines(doc_ptr doc, int count);
doc_region_t  doc_range_lines_from(doc_ptr doc, int top, int count);
doc_region_t  doc_range_bottom(doc_ptr doc, doc_pos_t start);
doc_region_t  doc_range_bottom_lines(doc_ptr doc, int count);
doc_region_t  doc_range_middle(doc_ptr doc, doc_pos_t start, doc_pos_t stop);
doc_region_t  doc_range_middle_lines(doc_ptr doc, int start_line, int stop_line);

int           doc_line_count(doc_ptr doc);
int           doc_width(doc_ptr doc);
int           doc_content_width(doc_ptr doc);

/*
 * Never lay this document out narrower than `cx`.
 *
 * A menu is framed and centred on the width of its own content, which is
 * right for a menu whose content does not change. It is wrong for one that
 * writes a line about whichever row the bar is on: every press of an arrow
 * changed the widest line, so the frame resized and the whole page slid left
 * and right under the cursor while you were trying to read it.
 *
 * Padding the line with spaces does not help -- content width counts ink, and
 * a trailing space is not ink. So the document is told how wide it is, once.
 */
void          doc_hold_width(doc_ptr doc, int cx);

/* The rows that stand for a keypress, and which of them is selected.
 *
 * A doc menu has always been driven by its letters. This is what lets it also
 * be walked with the arrow keys: the selection is an index into the hotspots,
 * it survives the menu being redrawn because the rows are rebuilt in the same
 * order, and Return simply presses the letter it is sitting on. */
int           doc_hotspot_count(doc_ptr doc);
char          doc_hotspot_key(doc_ptr doc, int index);
int           doc_selected(doc_ptr doc);
void          doc_select(doc_ptr doc, int index);

doc_pos_t     doc_next_bookmark(doc_ptr doc, doc_pos_t pos);
doc_pos_t     doc_prev_bookmark(doc_ptr doc, doc_pos_t pos);
/*
 * The next <topic:> whose name starts with this letter.
 *
 * This is what the orange letter in a section heading means: it is a key
 * you can press to get there, and doc_display_aux() has answered it for
 * years by treating any unhandled keystroke this way. Public so the
 * character screen can answer it too -- the headings it borrows from the
 * dump arrive with the letter already highlighted, and a highlight that
 * does nothing is worse than no highlight at all.
 */
doc_pos_t     doc_next_bookmark_char(doc_ptr doc, doc_pos_t pos, int c);
doc_pos_t     doc_find_bookmark(doc_ptr doc, cptr name);

doc_pos_t     doc_find_next(doc_ptr doc, cptr text, doc_pos_t start);
doc_pos_t     doc_find_prev(doc_ptr doc, cptr text, doc_pos_t start);

              /* <style:foo>Some Text<style:*> */
void          doc_push_style(doc_ptr doc, doc_style_ptr style);
void          doc_push_named_style(doc_ptr doc, cptr name);
void          doc_pop_style(doc_ptr doc);
doc_style_ptr doc_current_style(doc_ptr doc);

              /* Build a document from a text file */
doc_pos_t     doc_insert(doc_ptr doc, cptr text);
doc_pos_t     doc_read_file(doc_ptr doc, FILE *fp);
              enum { DOC_FORMAT_TEXT, DOC_FORMAT_HTML, DOC_FORMAT_DOC };
void          doc_write_file(doc_ptr doc, FILE *fp, int format);

              /* Build a document in code */
doc_pos_t     doc_insert_char(doc_ptr doc, byte a, char c);
doc_pos_t     doc_insert_text(doc_ptr doc, byte a, cptr text);
doc_pos_t     doc_insert_doc(doc_ptr dest_doc, doc_ptr src_doc, int indent);
doc_pos_t     doc_insert_cols(doc_ptr dest_doc, doc_ptr src_cols[], int col_count, int spacing);
doc_pos_t     doc_insert_space(doc_ptr dest_doc, int count);
doc_pos_t     doc_newline(doc_ptr doc);
void          doc_rollback(doc_ptr doc, doc_pos_t pos);
void          doc_clear(doc_ptr doc);

doc_pos_t     doc_printf(doc_ptr doc, const char *fmt, ...);
doc_pos_t     doc_cprintf(doc_ptr doc, byte a, const char *fmt, ...);

doc_char_ptr  doc_char(doc_ptr doc, doc_pos_t pos);
void          doc_sync_term(doc_ptr doc, doc_region_t range, doc_pos_t term_pos);
void          doc_sync_menu(doc_ptr doc);
/* A dedicated, opaque page for item/book selection and inspection. */
void          doc_sync_selection(doc_ptr doc);
/* As doc_sync_menu, but showing the page that starts at document row `top`,
 * so a page taller than the screen can scroll inside its own frame. */
void          doc_sync_menu_page(doc_ptr doc, int top);

/* Declare that the row being written stands for pressing `key`. When the
   document is painted, those rows are reported to the frontend, which is what
   lets a menu be clicked without changing how it draws or reads keys. */
void          doc_add_hotspot(doc_ptr doc, char key);
/* For a row the cursor has moved past -- see the note by the definition. */
void          doc_add_hotspot_at(doc_ptr doc, int row, char key);
/* Where the last menu was drawn -- see the note by the definition. */
doc_pos_t     doc_menu_origin(void);

/* Reserve a box for a picture of `name`, starting at the cursor. The caller is
 * responsible for leaving the box blank -- write the spaces, or lay it out as a
 * column, whichever suits the page. */
void          doc_add_portrait(doc_ptr doc, int collection, cptr name,
                               int cols, int rows, byte attr, char ch);

/* Mark the row the cursor is on as a rule. Leave it blank; a frontend draws
 * the line across it, and one that cannot leaves the blank. */
void          doc_add_rule(doc_ptr doc);

void          doc_change_name(doc_ptr doc, cptr name);
void          doc_change_html_header(doc_ptr doc, cptr header);
vec_ptr       doc_get_links(doc_ptr doc);

/* Parsing */
enum doc_tag_e
{
    DOC_TAG_NONE,
    DOC_TAG_COLOR,
    DOC_TAG_CLOSE_COLOR,
    DOC_TAG_STYLE,
    DOC_TAG_CLOSE_STYLE,
    DOC_TAG_TOPIC,
    DOC_TAG_LINK,
    DOC_TAG_VAR,
    DOC_TAG_INDENT,
    DOC_TAG_CLOSE_INDENT,
    DOC_TAG_TAB,
};
struct doc_tag_s
{
    int  type;
    cptr arg;
    int  arg_size;
};
typedef struct doc_tag_s doc_tag_t, *doc_tag_ptr;

enum doc_token_e
{
    DOC_TOKEN_EOF,
    DOC_TOKEN_TAG,
    DOC_TOKEN_WHITESPACE,
    DOC_TOKEN_NEWLINE,
    DOC_TOKEN_WORD,
};
struct doc_token_s
{
    int       type;
    cptr      pos;
    int       size;
    doc_tag_t tag;
};
typedef struct doc_token_s doc_token_t, *doc_token_ptr;

cptr doc_parse_tag(cptr pos, doc_tag_ptr tag);
cptr doc_lex(cptr pos, doc_token_ptr token);


int doc_display_help(cptr file_name, cptr topic);
int doc_display_help_aux(cptr file_name, cptr topic, rect_t display);
int doc_display(doc_ptr doc, cptr caption, int top);
int doc_display_aux(doc_ptr doc, cptr caption, int top, rect_t display);
int doc_display_tab(doc_ptr doc, cptr caption, int *top, void (*header)(rect_t));
int weapon_exp_display(doc_ptr doc, cptr caption, int *top);

/* True while a document owns wheel/resize input. */
extern bool doc_viewer_active(void);

#endif
