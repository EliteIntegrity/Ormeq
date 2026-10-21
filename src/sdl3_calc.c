/* Recorded calculations, painted on the frontend clock. No game calls or RNG.
 * Copyright (c) 2026 John Horton. Angband licence: see LICENSE. */
#include "sdl3_local.h"

static Uint64 _started;
static bool _animating;
static const melee_calc_blow_t *_card;
static rect_t _card_area;
static bool _replay;

bool sdl3_calc_card(const ui_event_t *e)
{
    _card = e->calculation;
    _card_area=rect(e->from.x,e->from.y,e->to.x,e->to.y);
    _replay = e->amount != 0;
    if (_replay) { _started = SDL_GetTicks(); _animating = TRUE; }
    sdl3_hover_dirty = TRUE;
    return TRUE;
}

bool sdl3_calc_event(void)
{
    _started = SDL_GetTicks();
    _animating = display_calculations_get() == DISPLAY_CALC_ANIMATED;
    sdl3_hover_dirty = TRUE;
    return TRUE;
}

static bool _visible(void)
{
    return display_calculations_get() != DISPLAY_CALC_OFF
        && ui_screen_map_owns() && sdl3_map_showing()
        && !sdl3_look_recall_active();
}

bool sdl3_calc_advance(void)
{
    if (!_animating) return FALSE;
    if ((!_visible() && !_card) || SDL_GetTicks()-_started >= 640) _animating = FALSE;
    /* One final frame removes the highlight, then the idle loop can sleep. */
    return TRUE;
}

static bool _has_record(void)
{
    int age,blow,position,total,omitted;
    const melee_calc_action_t *a;
    reckoner_live_current(&age,&blow,&position,&total,&omitted);
    a=melee_calc_get(age);
    return a && a->count;
}
float sdl3_calc_unit(void)
{
    return MIN((float)sdl3.cell_h,(ui_map_rect().cx-1)*sdl3.cell_w/35.0f);
}
static bool _compact(void) { return sdl3_calc_unit()<24.0f; }

SDL_FRect sdl3_calc_bounds(bool reserve)
{
    rect_t map = ui_map_rect();
    SDL_FRect r;
    float unit = sdl3_calc_unit();
    float margin = sdl3.cell_w*.5f;
    float left = (float)(map.x*sdl3.cell_w);
    float width = (float)(map.cx*sdl3.cell_w);
    r.w = MIN(width-2*margin, (_has_record() ? 39 : 28)*unit);
    r.h = (_compact() ? 3.3f : reserve || _has_record() ? 7.8f : 4.4f)*unit;
    r.x = left + margin;
    /* Fixed anchoring: status text appearing must not move the panel. */
    r.y = (sdl3.rows-(panel_status ? 1 : 0))*sdl3.cell_h-r.h-margin;
    return r;
}

bool sdl3_calc_contains(float x, float y)
{
    SDL_FRect r;
    if (!_visible()) return FALSE;
    r = sdl3_calc_bounds(FALSE);
    return x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h;
}

static SDL_FRect _history_button(SDL_FRect panel)
{
    float u=sdl3_calc_unit();
    SDL_FRect r={panel.x+panel.w-u*4.5f,panel.y+u*.18f,u*4.2f,u*1.12f};
    return r;
}

static SDL_FRect _result_button(SDL_FRect panel, bool next)
{
    SDL_FRect r=_history_button(panel);
    /* A fixed three-unit space keeps the counter centred and the controls
     * compact, without shifting the arrows as the result number changes. */
    r.w=sdl3_calc_unit()*1.55f;
    r.x-=sdl3_calc_unit()*(next ? 2 : 6.55f);
    return r;
}

/* Verbose screenshot diagnostics: hash the pixels actually rendered for
 * Details, so idle and direction-prompt frames can detect a flashing control. */
void sdl3_calc_capture(SDL_Surface *shot)
{
    SDL_FRect r;
    int ox, oy, x, y, x0, y0, x1, y1, bpp;
    u32b hash = 2166136261U;
    if (!_visible()) { printf("capture-reckoner 0\n"); return; }
    r = _history_button(sdl3_calc_bounds(FALSE));
    sdl3_grid_origin(&ox, &oy);
    x0 = MAX(0, (int)floor(r.x + ox)); y0 = MAX(0, (int)floor(r.y + oy));
    x1 = MIN(shot->w, (int)ceil(r.x + ox + r.w));
    y1 = MIN(shot->h, (int)ceil(r.y + oy + r.h));
    bpp = SDL_BYTESPERPIXEL(shot->format);
    for (y = y0; y < y1; ++y)
    {
        const byte *row = (const byte *)shot->pixels + y * shot->pitch;
        for (x = x0 * bpp; x < x1 * bpp; ++x) hash = (hash ^ row[x]) * 16777619U;
    }
    printf("capture-reckoner 1 %d %lu %.3f %.3f %.3f %.3f\n",
        inkey_flag && !character_icky ? 1 : 0, (unsigned long)hash, r.x, r.y, r.w, r.h);
}

bool sdl3_calc_click(float x, float y, int button)
{
    SDL_FRect h,r;
    if (!sdl3_calc_contains(x,y)) return FALSE;
    r=sdl3_calc_bounds(FALSE); h=_history_button(r);
    /* Only enqueue a command while the game is waiting for one. Look,
     * aiming and direction prompts must never consume a History click. */
    if (button == SDL_BUTTON_LEFT && inkey_flag && !character_icky)
    {
        int i,age,blow,position,total,omitted;
        reckoner_live_current(&age,&blow,&position,&total,&omitted);
        if(x>=h.x && x<h.x+h.w && y>=h.y && y<h.y+h.h)
            Term_keypress(SPECIAL_KEY_RECKONING);
        for(i=0;total>1 && i<2;++i)
        {
            h=_result_button(r,i!=0);
            if((i ? position<total : position>1) && x>=h.x && x<h.x+h.w && y>=h.y && y<h.y+h.h)
            {
                reckoner_live_move(i ? 1 : -1);
                _animating=FALSE; _started=0; sdl3_hover_dirty=TRUE;
            }
        }
    }
    return TRUE;
}

static void _colour(int role, int alpha)
{
    theme_color_t c = theme_ui(role);
    SDL_SetRenderDrawColor(sdl3.renderer, c.r,c.g,c.b,(Uint8)alpha);
}

static void _box(float x, float y, float w, float h, int role, int alpha)
{
    SDL_FRect r = {x,y,w,h};
    _colour(role,alpha);
    SDL_RenderFillRect(sdl3.renderer,&r);
}

static void _text(float x, float y, float h, float width, int role, cptr s)
{
    sdl3_atlas_entry *a = sdl3_atlas_for(h);
    SDL_Texture *tex = a ? a->tex : sdl3.atlas;
    theme_color_t c = theme_ui(role);
    float w = h * sdl3.cell_w / sdl3.cell_h;
    int i, max = (int)(width/w);
    SDL_SetTextureColorMod(tex,c.r,c.g,c.b);
    SDL_SetTextureAlphaMod(tex,255);
    for (i=0; s[i] && i<max; ++i)
        sdl3_draw_glyph_scaled(a,x+i*w,y,w,h,
            (int)strlen(s)>max && max>=3 && i>=max-3 ? '.' : s[i],ATLAS_TEXT);
}

static void _die(float x, float y, float side, int sides, int face)
{
    SDL_FRect r = {x,y,side,side};
    int row,col;
    _box(x,y,side,side,THEME_UI_SELECTION,110);
    _colour(THEME_UI_ACCENT,190);
    SDL_RenderRect(sdl3.renderer,&r);
    if (sides != 6)
    {
        char buf[16];
        float h=side*.76f, cw=h*sdl3.cell_w/sdl3.cell_h;
        strnfmt(buf,sizeof(buf),"%d",face);
        _text(x+(side-strlen(buf)*cw)/2,y+(side-h)/2,h,side,THEME_UI_TITLE,buf);
        return;
    }
    /* Six-sided dice use pips; other dice keep their actual numeric faces. */
    for (row=0;row<3;++row) for(col=0;col<3;++col)
    {
        bool pip = (row==1 && col==1 && (face&1))
            || (row==0 && col==0 && face>=2) || (row==2 && col==2 && face>=2)
            || (row==0 && col==2 && face>=4) || (row==2 && col==0 && face>=4)
            || (row==1 && col!=1 && face==6);
        float dot=MAX(2,side*.085f);
        if(pip) _box(x+side*(.25f+.25f*col)-dot/2,
            y+side*(.25f+.25f*row)-dot/2,dot,dot,THEME_UI_TITLE,255);
    }
}


static void _glyph(float x,float y,float h,byte attr,byte ch)
{
    sdl3_atlas_entry *a=sdl3_atlas_for(h);
    SDL_Texture *tex=a ? a->tex : sdl3.atlas;
    attr &= 0x7f;
    if(attr>=THEME_MAP_COLORS) attr=TERM_WHITE;
    SDL_SetTextureColorMod(tex,ormeq_color_table[attr][1],ormeq_color_table[attr][2],ormeq_color_table[attr][3]);
    SDL_SetTextureAlphaMod(tex,255);
    sdl3_draw_glyph_scaled(a,x,y,h*sdl3.cell_w/sdl3.cell_h,h,ch ? ch : '?',ATLAS_MAP);
}

static void _ribbon(float x,float y,float w,float h,int *values,int count,bool animate)
{
    int i,peak=1;
    float cw=w/count;
    Uint64 elapsed=SDL_GetTicks()-_started;
    theme_color_t color=theme_ui(THEME_UI_ACCENT);
    for(i=0;i<count;++i) peak=MAX(peak,values[i]);
    for(i=0;i<count-1;++i)
    {
        float left=h*MAX(0,values[i])/peak, right=h*MAX(0,values[i+1])/peak;
        float x1=x+cw*(i+.15f),x2=x+cw*(i+1.15f);
        SDL_FColor c={color.r/255.f,color.g/255.f,color.b/255.f,.6f};
        SDL_Vertex v[4]={{{x1,y-left/2},c,{0,0}},{{x2,y-right/2},c,{0,0}},
                         {{x2,y+right/2},c,{0,0}},{{x1,y+left/2},c,{0,0}}};
        int indices[]={0,1,2,0,2,3};
        _colour(THEME_UI_BORDER,180); SDL_RenderLine(sdl3.renderer,x1,y,x2,y);
        SDL_RenderGeometry(sdl3.renderer,NULL,v,4,indices,6);
        if(animate && elapsed<640)
        {
            int j;
            /* Display-clock sparks follow the recorded contour. No RNG, no
             * waits, and no animated numbers or implied extra damage rolls. */
            for(j=0;j<3;++j)
            {
                float t=(float)elapsed/640*(count-1)-i-j*.13f;
                if(t>=0 && t<1)
                {
                    float thick=left+(right-left)*t;
                    _box(x1+(x2-x1)*t,y+(j-1)*thick*.24f,MAX(1,h*.12f),MAX(1,h*.12f),THEME_UI_TITLE,220);
                }
                if(right<left)
                {
                    float t2=(float)elapsed/640*(count-1)-i-1-j*.07f;
                    if(t2>=0 && t2<.6f)
                        _box(x2+t2*cw*.32f,y+right/2+t2*h*(1+j*.3f),2,2,THEME_UI_MUTED,(int)(170*(1-t2/.6f)));
                }
            }
        }
    }
}

/* One stable account, shared by live and history. Height means the observed
 * amount on this card's scale, never remaining creature HP or probability. */
void sdl3_calc_diagram(const melee_calc_blow_t *b, SDL_FRect r, bool animate)
{
    float u=MIN(r.h/6.0f,r.w/36),pad=u*.5f,x=r.x+pad,y=r.y;
    float w=r.w-2*pad,cw=w/4;
    const reckoner_identity_t *id=&b->identity;
    char buf[240];
    int i,values[4];
    cptr labels[4];
    int nodes=reckoner_flow(b,values,labels);
    if(nodes) cw=w/nodes;
    if(u<22)
    {
        float line=MAX(12,MIN(18,r.h/5));
        reckoner_result(b,buf,sizeof(buf));
        _text(x,y,line,w,THEME_UI_TITLE,buf);
        strnfmt(buf,sizeof(buf),"%s / %s -> %s",id->actor,b->weapon,b->target);
        _text(x,y+line*1.3f,line,w,THEME_UI_TEXT,buf);
        if(nodes) strnfmt(buf,sizeof(buf),"%s %d -> %d %s",labels[0],values[0],b->damage,melee_calc_unit(b));
        else reckoner_reason(b,buf,sizeof(buf));
        _text(x,y+line*2.6f,line,w,THEME_UI_TEXT,buf);
        _text(x,y+line*3.9f,line,w,THEME_UI_MUTED,"Exact working below / E: full account");
        return;
    }

    /* Outcome first; the participants sit directly above their calculation. */
    reckoner_result(b,buf,sizeof(buf));
    _text(x,y,u*1.15f,w,THEME_UI_TITLE,buf);
    reckoner_reason(b,buf,sizeof(buf));
    _text(x,y+u*1.35f,u*.67f,w,THEME_UI_TEXT,buf);
    _glyph(x,y+u*2.25f,u*.9f,id->actor_attr,id->actor_ch);
    _text(x+u*.8f,y+u*2.4f,u*.65f,w*.21f,THEME_UI_TEXT,id->actor);
    _glyph(x+w*.26f,y+u*2.25f,u*.9f,id->tool_attr,id->tool_ch);
    _text(x+w*.26f+u*.8f,y+u*2.4f,u*.65f,w*.38f-u,THEME_UI_TEXT,b->weapon);
    _text(x+w*.66f,y+u*2.4f,u*.7f,u,THEME_UI_MUTED,">");
    _glyph(x+w*.70f,y+u*2.25f,u*.9f,id->target_attr,id->target_ch);
    _text(x+w*.70f+u*.8f,y+u*2.4f,u*.65f,w*.30f-u*.8f,THEME_UI_TEXT,b->target);
    if(nodes)
    {
        _ribbon(x,y+u*4.25f,w,u*.85f,values,nodes,animate);
        for(i=0;i<nodes;++i)
        {
            strnfmt(buf,sizeof(buf),"%d",values[i]);
            _text(x+cw*i,y+u*3.25f,u*.88f,cw-u*.2f,THEME_UI_TITLE,buf);
            if(i) strnfmt(buf,sizeof(buf),"%s (%+d)",labels[i],values[i]-values[i-1]);
            else strnfmt(buf,sizeof(buf),"%s",labels[i]);
            _text(x+cw*i,y+u*4.72f,u*.56f,cw-u*.2f,THEME_UI_MUTED,buf);
        }
    }
    else if(b->kind==RECKON_CAST && b->finished && b->visible)
    {
        float yy=y+u*4, rw=w*.6f;
        _box(x,yy,rw,2,THEME_UI_BORDER,255);
        if(b->power>=0 && b->power<=99)
            _box(x+rw*b->power/99,yy-u*.25f,1,u*.6f,THEME_UI_MUTED,255);
        _box(x+rw*MAX(0,MIN(99,b->gate))/99,yy-u*.18f,3,u*.4f,THEME_UI_ACCENT,255);
        strnfmt(buf,sizeof(buf),"Range 0-99 | marker: %d | %s",b->gate,
            b->power>99 ? "threshold above range: fails" : b->power<=0 ? "all draws pass" : "tick: failure threshold");
        _text(x,yy+u*.5f,u*.65f,w,THEME_UI_MUTED,buf);
    }
    else if(b->kind==RECKON_DEFENCE && b->finished && b->visible && b->player_known && b->target_known)
    {
        int last=-1;
        for(i=0;i<b->detail_count;++i) if(b->details[i].kind==RECKON_DETAIL_CHECK) last=i;
        if(last>=0)
        {
            const reckoner_detail_t *d=&b->details[last];
            _text(x,y+u*3.6f,u*.75f,w,THEME_UI_MUTED,d->label);
            strnfmt(buf,sizeof(buf),"%d %s %d: %s",d->left,d->op,d->right,d->result ? "true" : "false");
            _text(x,y+u*4.5f,u*.9f,w,THEME_UI_ACCENT,buf);
        }
        else for(i=0;i<b->detail_count;++i)
            if(b->details[i].kind==RECKON_DETAIL_ROLL)
            {
                const reckoner_detail_t *d=&b->details[i];
                strnfmt(buf,sizeof(buf),"%s: %d in %d..%d",d->label,d->left,d->low,d->high);
                _text(x,y+u*3.7f,u*.75f,w,THEME_UI_ACCENT,buf);
                break;
            }
    }
    else if(!b->hit && b->finished && b->visible && b->player_known && b->armor_known
        && b->supported && b->hit_checked && b->gate>=10 && b->power>0 && !b->hit_trait)
    {
        float yy=y+u*4, rw=w*.6f;
        int threshold=b->ac*3/4, high=b->power-1;
        _box(x,yy,rw,2,THEME_UI_BORDER,255);
        if(threshold>=0 && threshold<=high)
            _box(x+rw*threshold/MAX(1,high),yy-u*.25f,1,u*.6f,THEME_UI_MUTED,255);
        _box(x+rw*MAX(0,MIN(high,b->hit_roll))/MAX(1,high),yy-u*.18f,3,u*.4f,THEME_UI_ACCENT,255);
        strnfmt(buf,sizeof(buf),"Range 0-%d | marker: %d | %s",high,b->hit_roll,
            threshold>high ? "armour beyond range" : "tick: armour threshold");
        _text(x,yy+u*.5f,u*.65f,w,THEME_UI_MUTED,buf);
    }
    else
    {
        int shown=0;
        if(b->visible && b->player_known && b->target_known)
            for(i=0;i<b->detail_count && shown<2;++i)
            {
                const reckoner_detail_t *d=&b->details[i];
                if(d->kind==RECKON_DETAIL_STATUS_PUBLIC)
                    strnfmt(buf,sizeof(buf),"%s %s",d->label,!d->right ? "cleared" : !d->left ? "applied" : d->right>d->left ? "increased" : "reduced");
                else if(d->kind==RECKON_DETAIL_STATUS)
                    strnfmt(buf,sizeof(buf),"%s: %d -> %d (%+d counter)",d->label,d->left,d->right,d->right-d->left);
                else if(d->kind==RECKON_DETAIL_NOTE) strnfmt(buf,sizeof(buf),"%s",d->label);
                else continue;
                _text(x,y+u*(3.6f+shown*.8f),u*.75f,w,THEME_UI_ACCENT,buf); ++shown;
            }
        if(!shown && b->kind==RECKON_EFFECT && b->dice && b->player_known && b->target_known && b->visible)
        {
            strnfmt(buf,sizeof(buf),"%d effect power; this is not HP damage.",b->damage);
            _text(x,y+u*3.8f,u*.75f,w,THEME_UI_MUTED,buf);
        }
    }
    if(b->visible && b->player_known && b->target_known && b->dice && b->dd>0 && b->ds>0
        && b->base>=b->dd && b->base<=(double)b->dd*b->ds)
    {
        float yy=y+u*5.65f,rx=x+w*.68f,rw=w*.31f;
        int shown=MIN(b->dd,4);
        for(i=0;i<shown;++i) _die(x+i*u*.65f,yy,u*.55f,b->ds,b->faces[i]);
        strnfmt(buf,sizeof(buf),"%dd%d: %d | range %d-%.0f | avg %.1f",b->dd,b->ds,b->base,b->dd,(double)b->dd*b->ds,b->dd*(b->ds+1.0)/2);
        _text(x+u*3,yy,u*.58f,w*.65f-u*3,THEME_UI_MUTED,buf);
        _box(rx,yy+u*.3f,rw,1,THEME_UI_BORDER,255);
        _box(rx+rw/2,yy+u*.12f,1,u*.38f,THEME_UI_MUTED,255);
        _box(rx+rw*(b->ds==1 ? .5 : (b->base-b->dd)/((double)b->dd*(b->ds-1))),yy+u*.06f,3,u*.5f,THEME_UI_ACCENT,255);
    }
}

void sdl3_draw_calculation(void)
{
    const melee_calc_action_t *a;
    const melee_calc_blow_t *b;
    SDL_FRect r, inner;
    SDL_BlendMode old;
    char buf[180];
    float u;
    int selected,age,position,total,omitted;
    if (_card)
    {
        /* Keep a document card aligned with its cells when the font refits. */
        r.x=(float)(_card_area.x*sdl3.cell_w);
        r.y=(float)(_card_area.y*sdl3.cell_h);
        r.w=(float)(MAX(0,MIN(_card_area.cx,sdl3.cols-_card_area.x))*sdl3.cell_w);
        r.h=(float)(_card_area.cy*sdl3.cell_h);
        if(r.w>0)
        {
            SDL_GetRenderDrawBlendMode(sdl3.renderer,&old);
            SDL_SetRenderDrawBlendMode(sdl3.renderer,SDL_BLENDMODE_BLEND);
            _box(r.x,r.y,r.w,r.h,THEME_UI_PANEL,255);
            sdl3_calc_diagram(_card,r,_replay);
            SDL_SetRenderDrawBlendMode(sdl3.renderer,old);
        }
        return;
    }
    if(!_visible()) return;
    reckoner_live_current(&age,&selected,&position,&total,&omitted);
    a=melee_calc_get(age);
    b=a && a->count ? &a->blows[selected] : NULL;
    r=sdl3_calc_bounds(FALSE); u=sdl3_calc_unit();
    SDL_GetRenderDrawBlendMode(sdl3.renderer,&old);
    SDL_SetRenderDrawBlendMode(sdl3.renderer,SDL_BLENDMODE_BLEND);
    sdl3_hud_surface(r);
    strnfmt(buf,sizeof(buf),"The Reckoner");
    _text(r.x+u*.65f,r.y+u*.4f,u*.78f,r.w-u*3,THEME_UI_TEXT,buf);
    {
        SDL_FRect h=_history_button(r);
        /* Turn processing briefly drops inkey_flag on every movement. Keep
         * the control steady; sdl3_calc_click still guards actual input. */
        _box(h.x,h.y,h.w,h.h,THEME_UI_SELECTION,180);
        _colour(THEME_UI_BORDER,255); SDL_RenderRect(sdl3.renderer,&h);
        _text(h.x+u*.3f,h.y+u*.14f,u*.72f,h.w-u*.4f,THEME_UI_ACCENT,"Details");
    }
    if(total>1)
    {
        int i;
        SDL_FRect left=_result_button(r,FALSE);
        SDL_FRect right=_result_button(r,TRUE);
        float gap=right.x-left.x-left.w,h,tw;
        for(i=0;i<2;++i)
        {
            SDL_FRect h=_result_button(r,i!=0);
            bool enabled=i ? position<total : position>1;
            _box(h.x,h.y,h.w,h.h,THEME_UI_SELECTION,enabled ? 180 : 65);
            _colour(THEME_UI_BORDER,255); SDL_RenderRect(sdl3.renderer,&h);
            _text(h.x+u*.5f,h.y+u*.08f,u*.85f,h.w,enabled ? THEME_UI_ACCENT : THEME_UI_MUTED,enabled ? (i ? ">" : "<") : "-");
        }
        strnfmt(buf,sizeof(buf),"%d/%d%s",position,total,omitted ? "+" : "");
        h=MIN(u*.75f,(gap-u*.3f)*sdl3.cell_h/(strlen(buf)*sdl3.cell_w));
        tw=strlen(buf)*h*sdl3.cell_w/sdl3.cell_h;
        _text(left.x+left.w+(gap-tw)/2,left.y+(left.h-h)/2,h,gap,THEME_UI_TITLE,buf);
    }
    if(_compact())
    {
        if(b) reckoner_result(b,buf,sizeof(buf));
        else strnfmt(buf,sizeof(buf),"No calculations yet");
        _text(r.x+u*.65f,r.y+u*1.65f,MAX(12,u*.9f),r.w-u,THEME_UI_TITLE,buf);
        SDL_SetRenderDrawBlendMode(sdl3.renderer,old);
        return;
    }
    inner=r; inner.x+=4; inner.w-=8; inner.y+=u*1.3f; inner.h=u*5.5f;
    if(b) sdl3_calc_diagram(b,inner,display_calculations_get()==DISPLAY_CALC_ANIMATED);
    else
    {
        _text(r.x+u*.65f,r.y+u*1.5f,u*.95f,r.w-u,THEME_UI_TITLE,
            a ? "No calculation captured" : "No calculations yet");
        _text(r.x+u*.65f,r.y+u*2.6f,u*.72f,r.w-u,THEME_UI_MUTED,
            a ? "Some secondary effects are not recorded yet." : "Fight, cast or take damage to see the working.");
    }
    if(b && total>1)
    {
        strnfmt(buf,sizeof(buf),"Turn %d: %d recorded results%s  /  Click arrows or Details",
            a->turn,total,omitted ? " (more omitted)" : "");
        _text(r.x+u*.65f,r.y+u*7.18f,u*.62f,r.w-u,THEME_UI_MUTED,buf);
    }
    else _text(r.x+u*.65f,r.y+u*(b ? 7.18f : 3.65f),u*.62f,r.w-u,THEME_UI_MUTED,
        !b ? "Click Details for history  /  Ctrl+P: messages"
        : !b->visible || !b->player_known || !b->target_known ? "Detail concealed  /  Click Details for history"
        : b->kind==RECKON_CAST ? "Casting check  /  Click Details for history"
        : b->kind==RECKON_DEFENCE ? "Defence check  /  Click Details for history"
        : b->kind==RECKON_EFFECT ? "Effects and checks  /  Click Details for history"
        : melee_calc_complete(b) ? "Recorded chain  /  Click Details for history" : "Partial arithmetic  /  Click Details for history");
    SDL_SetRenderDrawBlendMode(sdl3.renderer,old);
}
