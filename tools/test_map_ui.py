"""Headless SDL map/settings captures and 32/64 camera regression checks.

Pass --keep DIRECTORY to retain screenshots. Uses disposable saves only.
"""
import argparse
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess

from game_runtime import GameRuntime, ROOT


def scenery_fixture(game):
    """A private town with readable terrain/object rows; never edit live data."""
    terrain=['SHALLOW_WATER','DEEP_WATER','SHALLOW_LAVA','DEEP_LAVA',
             'CLOSED_DOOR','OPEN_DOOR','BROKEN_DOOR','UP_STAIR','DOWN_STAIR','RUBBLE']
    objects=['wooden torch','brass lantern','ration of food','arrow','bolt',
             'iron shot','iron spike','flask of oil','small leather shield',
             'soft leather boots','hard leather armour','dagger',
             'corpse','skeleton','stone statue','magical figurine',
             'shard of pottery','black dragon scale mail']
    lines=['T:TOWN:NORMAL:NO_ROTATE','L:.:FLOOR(GLOW | MARK)',
           'L:<:UP_STAIR(GLOW | MARK)']
    rows=[list('.'*198) for _ in range(66)]
    for i,feature in enumerate(terrain):
        key=chr(ord('a')+i)
        lines.append(f'L:{key}:{feature}(GLOW | MARK)')
        rows[24][68+i*2]=key
    for i,obj in enumerate(objects):
        key=chr(ord('A')+i)
        lines.append(f'L:{key}:FLOOR(GLOW | MARK):OBJ({obj})')
        rows[28+(i//6)*4][70+(i%6)*3]=key
    rows[30][78]='<'
    lines+=['M:'+''.join(row) for row in rows]
    (game.root/'lib/edit/t_outp.txt').write_text('\n'.join(lines)+'\n')


def run_scenery(keep=None):
    env=dict(os.environ,SDL_VIDEO_DRIVER='dummy',SDL_AUDIO_DRIVER='dummy',SDL_RENDER_DRIVER='software')
    with GameRuntime() as game:
        scenery_fixture(game)
        fields,output,code=game.run(['-n','-Q','-uSCENERY','-s123','-x~n~~^;2%s'])
        assert code==0,output
        state={k:fields[k] for k in ('position','gold','experience','turns','hp','inventory','terrain')}
        shutil.copy2(ROOT/'ormeq-sdl3.exe',game.root)
        for dll in ROOT.glob('*.dll'): shutil.copy2(dll,game.root)
        shutil.copytree(ROOT/'font',game.root/'font')
        for name,mode,detail in [('scenery32',2,32),('scenery64',2,64),('scenery-hybrid',1,64),('scenery-glyphs',2,64)]:
            if name=='scenery-glyphs':
                pack=game.root/'lib/tiles/pack'
                hidden=pack.with_name('pack-unavailable')
                assert pack.resolve().is_relative_to(game.root) and hidden.resolve().is_relative_to(game.root)
                if pack.exists(): pack.rename(hidden)
            (game.root/'lib/user/display.txt').write_text(
                f'map_mode {mode}\ntile_detail {detail}\nsquare yes\nfont plex\nfullscreen no\n'
                'weather off\ndaylight no\nsound no\nmusic no\n')
            shot=game.root/(name+'.png')
            result=subprocess.run([str(game.root/'ormeq-sdl3.exe'),'-w','-f16','-W1280x720',
                '-z1.5','-uSCENERY','-P'+str(shot),'-x~%p%xq'],cwd=game.root,env=env,
                capture_output=True,text=True,timeout=35,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            assert result.returncode==0,result.stdout+result.stderr
            assert shot.read_bytes()[:8]==b'\x89PNG\r\n\x1a\n'
            if keep:
                Path(keep).mkdir(parents=True,exist_ok=True)
                shutil.copy2(shot,Path(keep)/shot.name)
        fields,output,code=game.run(['-Q','-uSCENERY','-x~'])
        assert code==0 and state=={k:fields[k] for k in state},output
    print('PASS: private scenery/item fixture, 32/64, Hybrid, missing-art glyphs and unchanged saved game')


def environment_fixture(game):
    """Ponds and special environments with the player standing on white snow."""
    features=['SHALLOW_WATER','DEEP_WATER','SHALLOW_LAVA','DEEP_LAVA',
              'SHALLOW_WASTE','DEEP_WASTE','SWAMP','SNOW_FLOOR','ICE_FLOOR',
              'SLUSH','SNOW_WALL','ICE_WALL','GLACIER','GLACIER_STEEP','CREVASSE',
              'DARK_PIT','GLASS_FLOOR','GLASS_WALL','PERMANENT_GLASS_WALL',
              'CLOSED_GLASS_DOOR','OPEN_GLASS_DOOR','BROKEN_GLASS_DOOR',
              'CLOSED_CURTAIN','OPEN_CURTAIN','WEB','SNOW_TREE']
    keys={feature:chr(ord('A')+i) for i,feature in enumerate(features)}
    lines=['T:TOWN:NORMAL:NO_ROTATE','L:.:FLOOR(GLOW | MARK)',
           'L:<:UP_STAIR(GLOW | MARK)']
    lines += [f'L:{key}:{feature}(GLOW | MARK)' for feature,key in keys.items()]
    rows=[list('.'*198) for _ in range(66)]
    def place(x,y,feature): rows[y][x]=keys[feature]
    for x,shallow,deep in [(64,'SHALLOW_WATER','DEEP_WATER'),
                           (70,'SHALLOW_LAVA','DEEP_LAVA'),
                           (76,'SHALLOW_WASTE','DEEP_WASTE'),(82,'SWAMP','SWAMP')]:
        for yy in range(4):
            for xx in range(5): place(x+xx,23+yy,deep if 0<xx<4 and 0<yy<3 else shallow)
    for yy in range(5):
        for xx in range(5): place(64+xx,29+yy,'SNOW_FLOOR')
    for x,y,feature in [(65,30,'ICE_FLOOR'),(66,30,'ICE_FLOOR'),(65,31,'ICE_FLOOR'),
                        (66,31,'CREVASSE'),(67,32,'SLUSH'),(64,29,'SNOW_WALL'),
                        (65,29,'ICE_WALL'),(66,29,'GLACIER'),(67,29,'GLACIER_STEEP')]:
        place(x,y,feature)
    for yy in range(5):
        for xx in range(5):
            feature='GLASS_WALL' if xx==0 or yy==0 or xx==4 else 'GLASS_FLOOR'
            place(71+xx,29+yy,feature)
    place(75,29,'PERMANENT_GLASS_WALL')
    for yy in range(3):
        for xx in range(3): place(81+xx,29+yy,'DARK_PIT')
    for xx in range(5): place(65+xx,36,'CLOSED_CURTAIN')
    place(67,36,'OPEN_CURTAIN')
    for yy in range(3): place(70,36+yy,'CLOSED_CURTAIN')
    place(70,37,'OPEN_CURTAIN')
    for i,feature in enumerate(features[19:]): place(72+i*2,34,feature)
    rows[30][78]='<'
    place(78,31,'SNOW_FLOOR')
    lines += ['M:'+''.join(row) for row in rows]
    (game.root/'lib/edit/t_outp.txt').write_text('\n'.join(lines)+'\n')


def run_environments(keep=None):
    env=dict(os.environ,SDL_VIDEO_DRIVER='dummy',SDL_AUDIO_DRIVER='dummy',SDL_RENDER_DRIVER='software')
    with GameRuntime() as game:
        environment_fixture(game)
        fields,output,code=game.run(['-n','-Q','-uENVIRONMENT','-s123','-x~n~~^;2%s'])
        assert code==0,output
        state={k:fields[k] for k in ('position','gold','experience','turns','hp','inventory','terrain')}
        shutil.copy2(ROOT/'ormeq-sdl3.exe',game.root)
        for dll in ROOT.glob('*.dll'): shutil.copy2(dll,game.root)
        shutil.copytree(ROOT/'font',game.root/'font')
        cameras={}
        cases=[('environment32',2,32,1280,720,1.5),('environment64',2,64,1280,720,1.5),
               ('environment-small',2,32,640,480,1.25),('environment-hybrid',1,64,1280,720,1.5),
               ('environment-glyphs',2,64,1280,720,1.5)]
        for name,mode,detail,width,height,zoom in cases:
            if name=='environment-glyphs':
                pack=game.root/'lib/tiles/pack'; hidden=pack.with_name('pack-unavailable')
                assert pack.resolve().is_relative_to(game.root) and hidden.resolve().is_relative_to(game.root)
                if pack.exists(): pack.rename(hidden)
            (game.root/'lib/user/display.txt').write_text(
                f'map_mode {mode}\ntile_detail {detail}\nsquare yes\nfont plex\nfullscreen no\n'
                'weather off\ndaylight no\nsound no\nmusic no\n')
            shot=game.root/(name+'.png')
            result=subprocess.run([str(game.root/'ormeq-sdl3.exe'),'-w','-f16',f'-W{width}x{height}',
                f'-z{zoom}','-uENVIRONMENT','-P'+str(shot),'-x~%p%xq'],cwd=game.root,env=env,
                capture_output=True,text=True,timeout=35,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            assert result.returncode==0,result.stdout+result.stderr
            data=shot.read_bytes()
            assert data[:8]==b'\x89PNG\r\n\x1a\n' and struct.unpack('>II',data[16:24])==(width,height)
            cameras[name]=[line.split()[1:] for line in result.stdout.splitlines() if line.startswith('capture-map ')][-1]
            if keep:
                Path(keep).mkdir(parents=True,exist_ok=True)
                shutil.copy2(shot,Path(keep)/shot.name)
        assert cameras['environment32'][:6]==cameras['environment64'][:6]==cameras['environment-glyphs'][:6]
        assert cameras['environment-glyphs'][6]=='0'
        fields,output,code=game.run(['-Q','-uENVIRONMENT','-x~'])
        assert code==0 and state=={k:fields[k] for k in state},output
    print('PASS: environment ponds, snow/player contrast, 32/64, 640x480, Hybrid, missing pack and unchanged saved game')


def marker_fixture(game):
    """Disposable courtyard and cellar; known markers among actual walls."""
    features=['GENERAL_STORE','ARMOURY','WEAPON_SMITHS','TEMPLE','ALCHEMY_SHOP',
              'MAGIC_SHOP','BLACK_MARKET','HOME','BOOKSTORE','MUSEUM','JEWELER',
              'SHROOMERY','DRAGONSKIN','QUEST_ENTER','ENTRANCE','TOWN_EXIT',
              'ARENA_GATE','SHAFT_UP','SHAFT_DOWN','TRAP_TRAPDOOR','TRAP_PIT',
              'TRAP_SLOW','TRAP_BLIND','TRAP_FIRE','TRAP_TY_CURSE','GLYPH',
              'MIRROR','SIGIL_START','SIGIL_1','SIGIL_2','SIGIL_END','SIGIL_OLD',
              'SIGIL_CORRUPTED','FLOWER','TRAP_BEAR','TRAP_ALARM','TRAP_ICICLE']
    keys={f:chr(65+i) for i,f in enumerate(features)}
    lines=['T:TOWN:NORMAL:NO_ROTATE','L:.:FLOOR(GLOW | MARK)',
           'L:#:PERMANENT(GLOW | MARK)','L:,:GRASS(GLOW | MARK)',
           'L:<:UP_STAIR(GLOW | MARK)','L:!:FLOOR(GLOW | MARK):OBJ(empty bottle)',
           'L:?:FLOOR(GLOW | MARK):MON(grey mold)']
    lines += [f'L:{key}:{feature}(GLOW | MARK)' for feature,key in keys.items()]
    rows=[list(','*198) for _ in range(66)]
    for y in range(21,44):
        for x in range(60,98): rows[y][x]='.'
    for i,f in enumerate(features[:13]):
        x,y=61+(i%7)*5,22+(i//7)*5
        for yy in range(y,y+3):
            for xx in range(x,x+4): rows[yy][xx]='#'
        rows[y+2][x+1]=keys[f]
    for x in range(60,98): rows[35][x]=rows[43][x]='#'
    rows[35][78]='.'
    for i,f in enumerate(features[13:]):
        rows[36+(i//8)*2][62+(i%8)*4]=keys[f]
    rows[30][78]='<'; rows[31][81]='!'; rows[32][76]='?'
    lines += ['M:'+''.join(row) for row in rows]
    (game.root/'lib/edit/t_outp.txt').write_text('\n'.join(lines)+'\n')


def run_markers(keep=None):
    env=dict(os.environ,SDL_VIDEO_DRIVER='dummy',SDL_AUDIO_DRIVER='dummy',SDL_RENDER_DRIVER='software')
    with GameRuntime() as game:
        marker_fixture(game)
        fields,output,code=game.run(['-n','-Q','-uMARKERS','-s123','-x~n~~^;2%s'])
        assert code==0,output
        shutil.copy2(ROOT/'ormeq-sdl3.exe',game.root)
        for dll in ROOT.glob('*.dll'): shutil.copy2(dll,game.root)
        shutil.copytree(ROOT/'font',game.root/'font')
        save=game.root/'lib/save/MARKERS'; original=save.read_bytes()
        state_keys=('position','gold','experience','turns','hp','inventory','terrain','randomness')
        cameras={}
        cases=[('markers32',2,32,1280,720,1.5,False),
               ('markers64',2,64,1280,720,1.5,False),
               ('markers-ascii-floors',2,64,1280,720,1.5,True),
               ('markers-small',2,32,640,480,1.25,False),
               ('markers-tiny',2,32,1280,720,.6,False),
               ('markers-hybrid',1,64,1280,720,1.5,False),
               ('markers-ascii',0,64,1280,720,1.5,False),
               ('markers-glyphs',2,64,1280,720,1.5,False)]
        for name,mode,detail,width,height,zoom,floors in cases:
            if name=='markers-glyphs':
                pack=game.root/'lib/tiles/pack'; hidden=pack.with_name('pack-unavailable')
                assert pack.resolve().is_relative_to(game.root) and hidden.resolve().is_relative_to(game.root)
                if pack.exists(): pack.rename(hidden)
            (game.root/'lib/user/display.txt').write_text(
                f'map_mode {mode}\ntile_detail {detail}\nascii_floors {"yes" if floors else "no"}\n'
                'square yes\nfont plex\nfullscreen no\nweather off\ndaylight no\nsound no\nmusic no\n')
            shot=game.root/(name+'.png')
            args=[str(game.root/'ormeq-sdl3.exe'),'-w','-f16',f'-W{width}x{height}',
                  f'-z{zoom}','-uMARKERS','-P'+str(shot)]
            results=[]
            for script in ('~%xq','~%p%xq'):
                save.write_bytes(original)
                result=subprocess.run(args+['-x'+script],cwd=game.root,env=env,
                    capture_output=True,text=True,timeout=35,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                assert result.returncode==0,result.stdout+result.stderr
                fields,output,code=game.run(['-Q','-uMARKERS','-x~'])
                assert code==0,output
                results.append({k:fields[k] for k in state_keys})
            assert results[0]==results[1],(name,results)
            assert struct.unpack('>II',shot.read_bytes()[16:24])==(width,height)
            cameras[name]=[line.split()[1:] for line in result.stdout.splitlines() if line.startswith('capture-map ')][-1]
            if keep:
                Path(keep).mkdir(parents=True,exist_ok=True)
                shutil.copy2(shot,Path(keep)/shot.name)
                (Path(keep)/(name+'.log')).write_text(result.stdout+result.stderr)
        assert cameras['markers32'][:6]==cameras['markers64'][:6]==cameras['markers-ascii-floors'][:6]==cameras['markers-glyphs'][:6]
        assert cameras['markers-glyphs'][6]=='0'
    print('PASS: marker courtyard/cellar, 32/64, ASCII floors, small/tiny zoom, ASCII/Hybrid, missing pack and unchanged state/RNG')


def run_look(keep=None):
    """Real Look documents/actions, including mouse replay and long lore."""
    env=dict(os.environ,SDL_VIDEO_DRIVER='dummy',SDL_AUDIO_DRIVER='dummy',SDL_RENDER_DRIVER='software')
    for long_lore in (False,True):
        with GameRuntime() as game:
            rows=[list('.'*198) for _ in range(66)]
            rows[30][78]='<'; rows[31][80]='m'
            plan=('T:TOWN:NORMAL:NO_ROTATE\nL:.:FLOOR(GLOW | MARK)\n'
                  'L:<:UP_STAIR(GLOW | MARK)\nL:m:FLOOR(GLOW | MARK):MON(grey mold)\n')
            (game.root/'lib/edit/t_outp.txt').write_text(plan+'\n'.join('M:'+''.join(r) for r in rows)+'\n')
            if long_lore:
                path=game.root/'lib/edit/r_info.txt'
                text=path.read_text(encoding='latin-1')
                marker='D:A small strange growth.'
                assert marker in text
                text=text.replace(marker,marker+'\n'+('D:An extended description in a disposable layout fixture tests scrolling and wrapping.\n'*40),1)
                path.write_text(text,encoding='latin-1')
            fields,output,code=game.run(['-n','-Q','-uCARD','-s123','-x~n~~^;2%s'])
            assert code==0,output
            state={k:fields[k] for k in ('position','gold','experience','turns','hp','inventory','terrain')}
            shutil.copy2(ROOT/'ormeq-sdl3.exe',game.root)
            for dll in ROOT.glob('*.dll'): shutil.copy2(dll,game.root)
            shutil.copytree(ROOT/'font',game.root/'font')
            (game.root/'lib/user/display.txt').write_text('map_mode 2\nascii_floors yes\nsquare yes\nfont plex\nfullscreen no\nweather off\ndaylight no\nsound no\nmusic no\n')
            def capture(name,width,height,script):
                shot=game.root/(name+'.png')
                args=[str(game.root/'ormeq-sdl3.exe'),'-w','-v','-f18',f'-W{width}x{height}',
                    '-uCARD','-P'+str(shot)]
                def launch(keys):
                    result=subprocess.run(args+['-x%!%!'+keys],cwd=game.root,env=env,capture_output=True,text=True,
                        timeout=35,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                    assert result.returncode==0,result.stdout+result.stderr
                    return result
                # Compare identical load/save cycles, so startup work is not
                # confused with randomness consumed by inspecting a monster.
                # This town fixture has no separately stored dungeon floors.
                save=game.root/'lib/save/CARD'; original=save.read_bytes()
                launch('~%xq')
                control,output,code=game.run(['-Q','-uCARD','-x~'])
                assert code==0,output
                save.write_bytes(original)
                result=launch(script)
                inspected,output,code=game.run(['-Q','-uCARD','-x~'])
                keys=tuple(state)+('randomness',)
                assert code==0 and {k:control[k] for k in keys}=={k:inspected[k] for k in keys}, (name,control,inspected)
                assert struct.unpack('>II',shot.read_bytes()[16:24])==(width,height)
                blocks=[]
                for line in result.stdout.splitlines():
                    if line.startswith('look-card '):
                        fields=line.split(maxsplit=10)
                        box=tuple(map(float,fields[1:5])); top,count,total,full,more=map(int,fields[5:10])
                        assert fields[10]=='Grey mold'
                        assert 0<=top<=total-count and 0<count<=total
                        assert box[0]>=0 and box[1]>=0 and box[0]+box[2]<=width+1 and box[1]+box[3]<=height+1
                        blocks.append(dict(box=box,top=top,rows=count,total=total,full=full,more=more,actions={}))
                    elif line.startswith('look-action '):
                        fields=line.split(); key=int(fields[1]); rect=tuple(map(float,fields[2:])); b=blocks[-1]
                        x,y,w,h=rect; bx,by,bw,bh=b['box']
                        assert x>=bx and y>=by and x+w<=bx+bw+.1 and y+h<=by+bh+.1
                        for ax,ay,aw,ah in b['actions'].values():
                            assert x>=ax+aw-.1 or ax>=x+w-.1 or y>=ay+ah-.1 or ay>=y+h-.1
                        b['actions'][key]=rect
                assert blocks,result.stdout
                for b in blocks:
                    if not b['more']:
                        assert set(b['actions'])=={45,43,114,9,113,63}
                        assert len({r[1] for r in b['actions'].values()})==1
                if keep:
                    Path(keep).mkdir(parents=True,exist_ok=True)
                    shutil.copy2(shot,Path(keep)/shot.name)
                    (Path(keep)/(name+'.log')).write_text(result.stdout+result.stderr)
                return blocks
            if long_lore:
                for name,extra in [('look-scroll',''),('look-scroll-full','r')]:
                    blocks=capture(name,640,480,'~l+'+extra+r'\]'*220+'%p^%xq')
                    assert blocks[-1]['top']>0 and blocks[-1]['top']+blocks[-1]['rows']==blocks[-1]['total']
            else:
                capture('look-card',1600,1000,'~l+%p^%xq')
                capture('look-small',640,480,'~l+%p^%xq')
                full=capture('look-full',640,480,'~l+r%p^%xq'); assert full[-1]['full']==1
                more=capture('look-more',1280,720,'~l+\t%p^%xq'); assert more[-1]['more']==1
                back=capture('look-roundtrip',1280,720,'~l+rR\t\t%p^%xq')
                assert back[-1]['full']==0 and back[-1]['more']==0
                # Click the actual geometry, not a test-only action shortcut.
                for name,key,flag in [('look-click-more',9,'more'),('look-click-full',114,'full')]:
                    x,y,w,h=more[0]['actions'][key]
                    blocks=capture(name,1280,720,f'~%!l+%!%@{x+w/2:.2f},{y+h/2:.2f};%!%p^%xq')
                    assert blocks[-1][flag]==1
                # An unrelated click while Look owns input cannot move the player.
                capture('look-click-outside',1280,720,'~l+%@1000,600;%p^%xq')
                art=game.root/'lib/art'; hidden=art.with_name('art-unavailable')
                assert art.resolve().is_relative_to(game.root) and hidden.resolve().is_relative_to(game.root)
                if art.exists(): art.rename(hidden)
                capture('look-no-art',1280,720,'~l+%p^%xq')
            fields,output,code=game.run(['-Q','-uCARD','-x~'])
            assert code==0 and state=={k:fields[k] for k in state}, (state, fields, output)
    print('PASS: Look cards, fullscreen/More/scroll, clickable text bounds, mouse replay, glyph fallback and unchanged game/RNG')


def run_resets(keep=None):
    """Reset captures happen inside dispatch, before input or a forced fresh."""
    env=dict(os.environ,SDL_VIDEO_DRIVER='dummy',SDL_AUDIO_DRIVER='dummy',SDL_RENDER_DRIVER='software')
    with GameRuntime() as game:
        fields,output,code=game.run(['-n','-Q','-uRESET','-s123','-x~n~~^;2%s'])
        assert code==0,output
        original={k:fields[k] for k in ('position','gold','experience','turns','hp','inventory','terrain')}
        save=game.root/'lib/save/RESET'; saved=save.read_bytes()
        shutil.copy2(ROOT/'ormeq-sdl3.exe',game.root)
        for dll in ROOT.glob('*.dll'): shutil.copy2(dll,game.root)
        shutil.copytree(ROOT/'font',game.root/'font')
        cases=[('ascii',0,64,False,1280,720,'~','%xq'),
               ('full32',2,32,False,1280,720,'~','%xq'),
               ('full64',2,64,False,1280,720,'~','%xq'),
               ('ascii-floors-small',2,64,True,640,480,'~','%xq'),
               ('settings',2,64,False,800,600,'~^s','xc%xq'),
               ('help',2,64,False,1280,720,'~?','^%xq'),
               ('messages',2,64,False,1280,720,'~%P','^%xq'),
               ('history',2,64,False,640,480,'~%P%i','^%xq'),
               ('look',2,64,False,1280,720,'~l+','^%xq')]
        for name,mode,detail,ascii_floors,width,height,prefix,suffix in cases:
            before=None; control=None; menu_click=''
            for event,command in [('before','%p'),('device','%0'),('targets','%9')]:
                save.write_bytes(saved)
                (game.root/'lib/user/display.txt').write_text(
                    f'map_mode {mode}\ntile_detail {detail}\nascii_floors {"yes" if ascii_floors else "no"}\n'
                    'square yes\nfont plex\nfullscreen no\nweather off\ndaylight no\nsound no\nmusic no\n')
                shot=game.root/f'reset-{name}-{event}.png'
                result=subprocess.run([str(game.root/'ormeq-sdl3.exe'),'-w','-v','-f16',f'-W{width}x{height}',
                    '-uRESET','-P'+str(shot),'-x'+prefix+command+menu_click+suffix],cwd=game.root,env=env,
                    capture_output=True,text=True,timeout=35,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                assert result.returncode==0,result.stdout+result.stderr
                data=shot.read_bytes()
                if keep:
                    Path(keep).mkdir(parents=True,exist_ok=True)
                    shutil.copy2(shot,Path(keep)/shot.name)
                    (Path(keep)/(shot.stem+'.log')).write_text(result.stdout+result.stderr)
                if before is None:
                    before=data
                    if name=='settings':
                        # Click the floor checkbox after each reset, using
                        # the baseline menu's actual cell size and panel.
                        cells=re.findall(r'cell (\d+)x(\d+)\)',result.stdout)
                        cw,ch=map(int,cells[-1])
                        panels=re.findall(r'panel (\d+),(\d+) \d+x24 ',result.stdout)
                        px,py=map(int,panels[-1])
                        menu_click=f'%@{(px+6.5)*cw},{(py+16.5)*ch};'
                else:
                    assert data==before, f'{name}: {event} reset did not immediately restore the same pixels'
                    if name=='settings':
                        assert 'ascii_floors yes' in (game.root/'lib/user/display.txt').read_text(), 'Menu click lost after reset'
                fields,output,code=game.run(['-Q','-uRESET','-x~'])
                assert code==0 and original=={k:fields[k] for k in original},output
                if control is None: control=fields['randomness']
                else: assert fields['randomness']==control, (name,event,'RNG changed')
    print('PASS: 27 immediate reset captures match for ASCII/Full Tile, HUD, menus, help, messages, history and Look; unchanged game/RNG')


def run(keep=None):
    env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy',
               SDL_RENDER_DRIVER='software')
    with GameRuntime() as game:
        fields, output, code = game.run(['-n','-Q','-uSPRITES','-s123','-x~n~~^;2%s'])
        assert code == 0, output
        original = {k: fields[k] for k in ('position','gold','experience','turns','hp','inventory','terrain')}
        shutil.copy2(ROOT/'ormeq-sdl3.exe',game.root)
        for dll in ROOT.glob('*.dll'):
            shutil.copy2(dll,game.root)
        shutil.copytree(ROOT/'font',game.root/'font')
        cameras = {}
        cases = [('classic',0,64,False,False), ('square',0,64,True,False),
                 ('enhanced',0,64,True,True), ('hybrid32',1,32,True,False),
                 ('hybrid64',1,64,True,False), ('full32',2,32,True,False),
                 ('full64',2,64,True,False), ('reset',2,64,True,False),
                 ('small',2,64,True,False), ('ascii-floors',2,64,True,False), ('missing',2,64,True,False)]
        for name, mode, detail, square, enhanced in cases:
            if name == 'missing':
                pack=game.root/'lib/tiles/pack'
                hidden=pack.with_name('pack-unavailable')
                assert pack.resolve().is_relative_to(game.root) and hidden.resolve().is_relative_to(game.root)
                if pack.exists():
                    pack.rename(hidden)
            (game.root/'lib/user/display.txt').write_text(
                f'map_mode {mode}\ntile_detail {detail}\nsquare {"yes" if square else "no"}\n'
                f'enhanced_square {"yes" if enhanced else "no"}\n'
                f'ascii_floors {"yes" if name=="ascii-floors" else "no"}\n'
                'font plex\nfullscreen no\nweather off\ndaylight no\nsound no\nmusic no\n')
            shot=game.root/(name+'.png')
            dimensions=(640,480) if name=='small' else (1280,720)
            script='~%0%xq' if name=='reset' else '~%p%xq'
            result=subprocess.run([str(game.root/'ormeq-sdl3.exe'),'-w','-f16','-W%dx%d'%dimensions,
                '-uSPRITES','-P'+str(shot),'-x'+script],cwd=game.root,env=env,
                capture_output=True,text=True,timeout=35,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            assert result.returncode == 0, result.stdout+result.stderr
            data=shot.read_bytes()
            assert data[:8] == b'\x89PNG\r\n\x1a\n' and struct.unpack('>II',data[16:24]) == dimensions
            rows=[line.split()[1:] for line in result.stdout.splitlines() if line.startswith('capture-map ')]
            assert rows, result.stdout
            cameras[name]=rows[-1]
            if keep:
                Path(keep).mkdir(parents=True,exist_ok=True)
                shutil.copy2(shot,Path(keep)/shot.name)
        assert cameras['square'][:6] == cameras['enhanced'][:6]
        assert cameras['hybrid32'][:6] == cameras['hybrid64'][:6]
        assert cameras['full32'][:6] == cameras['full64'][:6] == cameras['missing'][:6]
        assert cameras['full64']==cameras['ascii-floors']
        assert cameras['missing'][6] == '0'
        assert cameras['reset'] == cameras['full64']
        fields,output,code=game.run(['-Q','-uSPRITES','-x~'])
        assert code == 0 and original == {k:fields[k] for k in original},output
        # Real settings controls, persistence and a small-screen layout.
        (game.root/'lib/user/display.txt').write_text('map_mode 0\nsquare yes\nfullscreen no\n')
        shot=game.root/'settings.png'
        result=subprocess.run([str(game.root/'ormeq-sdl3.exe'),'-w','-f14','-W800x600',
            '-P'+str(shot),'-xseiririqq%pxq'],cwd=game.root,env=env,capture_output=True,text=True,
            timeout=35,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        assert result.returncode == 0,result.stdout+result.stderr
        prefs=(game.root/'lib/user/display.txt').read_text()
        assert 'enhanced_square yes' in prefs
        values=dict(line.split() for line in prefs.splitlines() if line and not line.startswith('#'))
        assert values['map_mode']=='0' and values['tile_detail']=='64' and values['square']=='yes'
        if keep:
            shutil.copy2(shot,Path(keep)/shot.name)
        (game.root/'lib/user/display.txt').write_text('map_mode 2\nascii_floors no\nsquare yes\nfullscreen no\n')
        shot=game.root/'settings-ascii-floors.png'
        result=subprocess.run([str(game.root/'ormeq-sdl3.exe'),'-w','-f14','-W800x600',
            '-P'+str(shot),'-xsa%pxq'],cwd=game.root,env=env,capture_output=True,text=True,
            timeout=35,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        assert result.returncode==0,result.stdout+result.stderr
        assert 'ascii_floors yes' in (game.root/'lib/user/display.txt').read_text()
        if keep: shutil.copy2(shot,Path(keep)/shot.name)
    print('PASS: 11 SDL map captures, ASCII floors, resolution/missing-pack/reset camera invariance, unchanged saved game and small settings')
    run_scenery(keep)
    run_environments(keep)
    run_markers(keep)
    run_look(keep)
    run_resets(keep)


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--keep',type=Path)
    run(parser.parse_args().keep)
