/* Past Reckonings browser. Copyright (c) 2026 John Horton.
 * Angband licence: see LICENSE. */
#include "scottish.h"

static bool _history_active;
bool reckoner_history_active(void) { return _history_active; }

static void _history_title(const melee_calc_action_t *a,char *buf,int size)
{
    const melee_calc_blow_t *b=a->count ? &a->blows[melee_calc_live_blow(a)] : NULL;
    if(!b) strnfmt(buf,size,"Other effects");
    else if(b->identity.incoming) strnfmt(buf,size,"%s -> You",b->identity.actor);
    else strnfmt(buf,size,"%s -> %s",b->weapon,b->target);
}

static void _history_button(int x,int y,cptr text,int key)
{
    c_put_str(TERM_YELLOW,text,y,x);
    ui_event_menu_item(x,y,(int)strlen(text),key);
}

static int _account(doc_ptr doc,const melee_calc_blow_t *b,int index,int count)
{
    char buf[240];
    int diagram;
    doc_printf(doc,"<color:B>%s</color>  |  %d of %d result%s\n",reckoner_result_kind(b),index+1,count,count==1 ? "" : "s");
    diagram=doc_cursor(doc).y;
    reckoner_result(b,buf,sizeof(buf)); doc_cprintf(doc,TERM_L_GREEN,"%s\n",buf);
    reckoner_reason(b,buf,sizeof(buf)); doc_printf(doc,"%s\n",buf);
    doc_printf(doc,"%.16s / %.24s -> %.24s\n",b->identity.actor,b->weapon,b->target);
    /* Text fallback remains useful in terminal frontends and while the diagram
     * is partly scrolled offscreen. SDL3 paints the same account over this. */
    if(b->dice && b->visible && b->player_known && b->target_known && b->hit && b->kind!=RECKON_CAST)
    {
        if(b->dd) doc_printf(doc,"Dice %dd%d = %d\n",b->dd,b->ds,b->base);
        else doc_printf(doc,"Base %d\n",b->base);
        doc_printf(doc,"Resolved: %d %s\n",b->damage,melee_calc_unit(b));
    }
    while(doc_cursor(doc).y<diagram+8) doc_newline(doc);
    if(b->kind!=RECKON_EFFECT && b->kind!=RECKON_CAST && b->hit && b->dice && b->visible && b->player_known && b->target_known)
    {
        int i;
        doc_insert(doc,"<color:U>How the amount changed</color>\n");
        doc_printf(doc,"%s: %d\n",b->dd ? "Dice total" : "Starting value",b->base);
        for(i=0;i<b->step_count;++i)
        {
            const melee_calc_step_t *step=&b->steps[i];
            doc_printf(doc,"%s: %d -> %d (%+d)%s\n",step->label,step->before,step->after,
                step->after-step->before,step->exact ? "" : " [formula unrecorded]");
        }
        doc_printf(doc,"%s: %d\n",melee_calc_unit(b),b->damage);
        doc_printf(doc,"<color:s>%s</color>\n",melee_calc_complete(b) ?
            "Recorded arithmetic joins from the source to this result." : melee_calc_explanation(b));
    }
    spell_calc_details(b,doc);
    if(b->steps_omitted) doc_insert(doc,"Further arithmetic omitted (record limit).\n");
    return diagram;
}

int melee_calc_show(int *saved_age,int *saved_blow,bool *focus)
{
    int age=*saved_age,blow=*saved_blow,result=0,top=0;
    bool replay=FALSE,list=!*focus;
    _history_active=TRUE;
    screen_save();
    sound(SOUND_UI_OPEN);
    for(;;)
    {
        rect_t page=ui_page_rect(),body;
        bool wide=page.cx>=92 && page.cy>=19;
        bool listing=wide || list;
        const melee_calc_action_t *a=melee_calc_get(age);
        const melee_calc_blow_t *b;
        int cmd,i,start=0,rows=MAX(1,MIN(9,(page.cy-8)/3));
        int diagram=-1,max_top=0;
        int old_age=age,old_blow=blow,old_top=top;
        bool old_list=list;
        char buf[240],title[180];
        doc_ptr doc;
        if(a && blow>=a->count) blow=MAX(0,a->count-1);
        b=a && a->count ? &a->blows[blow] : NULL;
        body=rect(page.x+(wide ? 34 : 0),page.y+4,page.cx-(wide ? 34 : 0),MAX(1,page.cy-8));
        doc=doc_alloc(body.cx);
        ui_event_calculation_card(NULL,rect(0,0,0,0),FALSE);
        ui_event_map_hidden(); ui_event_menu_clear(); ui_event_panel_screen();
        ui_event_scroll(FALSE,FALSE);
        Term_clear_rect(ui_screen_rect());
        ui_history_tabs(1,page);
        if(a) strnfmt(buf,sizeof(buf),"Recent %d/%d  |  Turn %d",age+1,melee_calc_count(),a->turn);
        else strnfmt(buf,sizeof(buf),"No recorded turns");
        c_put_str(TERM_SLATE,buf,page.y+2,page.x);
        if(listing && melee_calc_count())
        {
            int width=wide ? 31 : page.cx;
            start=MIN(MAX(0,age-rows/2),MAX(0,melee_calc_count()-rows));
            for(i=0;i<rows && start+i<melee_calc_count();++i)
            {
                const melee_calc_action_t *item=melee_calc_get(start+i);
                const melee_calc_blow_t *last=item->count ? &item->blows[melee_calc_live_blow(item)] : NULL;
                int y=body.y+i*3;
                _history_title(item,title,sizeof(title));
                strnfmt(buf,sizeof(buf),"%c#%u %.150s",start+i==age ? '>' : ' ',item->serial,title);
                if((int)strlen(buf)>width) { buf[width]=0; if(width>=3) memcpy(buf+width-3,"...",3); }
                c_put_str(start+i==age ? TERM_YELLOW : TERM_WHITE,buf,y,page.x);
                if(last) reckoner_result(last,title,sizeof(title)); else strcpy(title,"No numeric result captured");
                strnfmt(buf,sizeof(buf),"   T%d | %s",item->turn,title); if((int)strlen(buf)>width) { buf[width]=0; if(width>=3) memcpy(buf+width-3,"...",3); }
                c_put_str(TERM_SLATE,buf,y+1,page.x);
                ui_event_menu_item(page.x,y,width,SPECIAL_KEY_HISTORY_ROW+i);
                ui_event_menu_item(page.x,y+1,width,SPECIAL_KEY_HISTORY_ROW+i);
            }
        }
        if(!listing || wide || !melee_calc_count())
        {
            if(b) diagram=_account(doc,b,blow,a->count);
            else doc_insert(doc,"<color:U>No calculations recorded</color>\n\nFight, cast a spell or take damage to create an account.\nHistory is available with The Reckoner switched off.\n");
            if(a && a->other) doc_insert(doc,"\nOther effects occurred outside the recorded calculations.\n");
            if(a && a->omitted) doc_printf(doc,"\n%d further results omitted (record limit).\n",a->omitted);
            max_top=MAX(0,doc_cursor(doc).y-body.cy);
            top=MAX(0,MIN(top,max_top));
            doc_sync_term(doc,doc_range_lines_from(doc,top,body.cy),doc_pos_create(body.x,body.y));
            if(diagram>=top && diagram+8<=top+body.cy)
                ui_event_calculation_card(b,rect(body.x,body.y+diagram-top,body.cx,8),replay);
            ui_event_scroll(top>0,top<max_top);
        }
        /* Controls are compact, clickable, and outside the scrolling account. */
        i=page.y+page.cy-3;
        _history_button(page.x,i,"[P Newer]",'p');
        _history_button(page.x+11,i,"[N Older]",'n');
        if(!wide) _history_button(page.x+22,i,list ? "[Enter Open]" : "[L List]",list ? '\r' : 'l');
        if(!list || wide)
        {
            _history_button(page.x+36,i,"[ Prev result]",'[');
            _history_button(page.x+52,i,"[Next result ]",']');
        }
        _history_button(page.x,i+1,"[E Exact/export]",'e');
        _history_button(page.x+18,i+1,"[R Replay]",'r');
        _history_button(page.x+30,i+1,"[? Help]",'?');
        _history_button(page.x+40,i+1,(!list && !wide) ? "[Esc List]" : "[Esc Close]",ESCAPE);
        c_put_str(TERM_SLATE,list && !wide ? "Select a row to read its results." : "Up/Down: scroll  |  PgUp/PgDn: page",i+2,page.x);
        Term_dirty();
        cmd=inkey_special(FALSE);
        ui_event_calculation_card(NULL,rect(0,0,0,0),FALSE);
        doc_free(doc); replay=FALSE;
        if(cmd=='\t') { sound(SOUND_UI_TAB); result='\t'; break; }
        if(cmd==ESCAPE && !list && !wide) { sound(SOUND_UI_BACK); list=TRUE; top=0; continue; }
        if(cmd==ESCAPE || cmd=='x' || cmd=='X') break;
        if(cmd=='?') doc_display_help("reckoner.txt",NULL);
        if(cmd=='l' || cmd=='L') { list=!list; top=0; }
        if(cmd=='\r' || cmd=='\n') { list=FALSE; top=0; }
        if(listing && cmd>=SPECIAL_KEY_HISTORY_ROW && cmd<SPECIAL_KEY_HISTORY_ROW+rows && start+cmd-SPECIAL_KEY_HISTORY_ROW<melee_calc_count())
        { age=start+cmd-SPECIAL_KEY_HISTORY_ROW; blow=melee_calc_live_blow(melee_calc_get(age)); list=FALSE; top=0; }
        /* Numpad navigation matches other document viewers; row serials are
         * identifiers, not unstable keyboard shortcuts. */
        if(cmd=='8') cmd=SKEY_UP;
        if(cmd=='2') cmd=SKEY_DOWN;
        if(cmd=='9') cmd=SKEY_PGUP;
        if(cmd=='3') cmd=SKEY_PGDOWN;
        if(cmd=='7') cmd=SKEY_TOP;
        if(cmd=='1') cmd=SKEY_BOTTOM;
        if((cmd=='n' || cmd=='N' || (list && !wide && cmd==SKEY_DOWN)) && age+1<melee_calc_count())
        { ++age; blow=0; top=0; }
        if((cmd=='p' || cmd=='P' || (list && !wide && cmd==SKEY_UP)) && age>0)
        { --age; blow=0; top=0; }
        if(cmd=='r' || cmd=='R') { replay=TRUE; list=FALSE; top=0; }
        if(a && cmd==']' && blow+1<a->count) { ++blow; list=FALSE; top=0; }
        if(a && cmd=='[' && blow>0) { --blow; list=FALSE; top=0; }
        if(!list || wide)
        {
            if(cmd==SKEY_DOWN) ++top;
            if(cmd==SKEY_UP) --top;
            if(cmd==SKEY_PGDOWN || cmd==' ') top+=body.cy-1;
            if(cmd==SKEY_PGUP) top-=body.cy-1;
            if(cmd==SKEY_TOP) top=0;
            if(cmd==SKEY_BOTTOM) top=max_top;
        }
        else if(cmd==SKEY_PGDOWN || cmd==SKEY_PGUP)
        { age=MAX(0,MIN(melee_calc_count()-1,age+(cmd==SKEY_PGDOWN ? rows : -rows))); blow=0; }
        if(age!=old_age || blow!=old_blow || MAX(0,MIN(top,max_top))!=old_top)
            sound(SOUND_UI_NAVIGATE);
        else if(list!=old_list || replay) sound(SOUND_UI_ACCEPT);
        if(a && (cmd=='e' || cmd=='E'))
        {
            doc_ptr detail=doc_alloc(80);
            string_append_s(detail->name,"past-reckonings.txt");
            melee_calc_document(a,detail);
            doc_display(detail,"Past Reckonings: exact working",0);
            doc_free(detail);
        }
    }
    *saved_age=age; *saved_blow=blow; *focus=!list;
    _history_active=FALSE;
    if(result!='\t') sound(SOUND_UI_CLOSE);
    screen_load();
    return result;
}
