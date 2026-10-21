# Ormeq build (MSVC / Windows).
#
#   cmake -S . -B out
#   cmake --build out --config Release
#
# The executable is written to the repo root on purpose: init_stuff() in
# main-win.c locates game data by appending "lib\\" to the executable's own
# directory, so ormeq.exe must sit next to lib/.

cmake_minimum_required(VERSION 3.20)
#
# Link the C runtime into the executable rather than beside it.
#
# The default is the DLL runtime, which means the machine needs the Visual C++
# redistributable installed -- and a folder you copy onto an old laptop cannot
# assume that. It fails at launch with a missing VCRUNTIME140.dll and no
# explanation anybody outside this trade would recognise.
#
# The SDL3 DLLs are already built this way and need nothing, so linking ours
# statically makes the whole folder self-contained: copy it, double-click it,
# play it.
#
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The public version is independent of the inherited save-format version.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/defines.h" ORMEQ_VERSION_LINE
     REGEX "^#define ORMEQ_VER +\"[0-9]+\\.[0-9]+\\.[0-9]+\"")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" ORMEQ_VERSION "${ORMEQ_VERSION_LINE}")
if(NOT ORMEQ_VERSION)
    message(FATAL_ERROR "No semantic ORMEQ_VER found in src/defines.h")
endif()
project(ormeq VERSION ${ORMEQ_VERSION} LANGUAGES C)

if(NOT WIN32)
    message(FATAL_ERROR "This CMakeLists currently targets Windows only.")
endif()

# ---------------------------------------------------------------- source list
# Shared engine sources for the Windows, SDL3 and headless frontends.
set(ANGBAND_SOURCES
    src/c-string.c
    src/c-vec.c
    src/z-doc.c
    src/z-form.c
    src/z-rand.c
    src/z-term.c
    src/z-util.c
    src/z-virt.c
    src/alky.c
    src/archer.c
    src/arky.c
    src/artifact.c
    src/autopick.c
    src/ui_loot.c
    src/virtue.c
    src/bard.c
    src/beastmaster.c
    src/bearskin.c
    src/berserker.c
    src/birth.c
    src/bldg.c
    src/blood_knight.c
    src/blood_mage.c
    src/bluemage.c
    src/cavalry.c
    src/cave.c
    src/chaoswar.c
    src/classes.c
    src/cmd1.c
    src/cmd2.c
    src/cmd3.c
    src/cmd4.c
    src/cmd5.c
    src/cmd6.c
    src/combat.c
    src/ui_reckoner.c
    src/ui_quest.c
    src/melee_calc.c
    src/spell_calc.c
    src/corny.c
    src/demigod.c
    src/devicemaster.c
    src/devices.c
    src/disciple.c
    src/do-spell.c
    src/doppelganger.c
    src/duelist.c
    src/dungeon.c
    src/ego.c
    src/effects.c
    src/equip.c
    src/fear.c
    src/files.c
    src/flavor.c
    src/floors.c
    src/force_trainer.c
    src/generate.c
    src/gf.c
    src/graymage.c
    src/grid.c
    src/hex.c
    src/highmage.c
    src/hissatsu.c
    src/home.c
    src/igor.c
    src/init1.c
    src/init2.c
    src/int-map.c
    src/inv.c
    src/karrot.c
    src/lawyer.c
    src/load.c
    src/mage.c
    src/magic_eater.c
    src/mauler.c
    src/melee1.c
    src/melee2.c
    src/menu.c
    src/message.c
    src/mindcrafter.c
    src/mirror_master.c
    src/mon.c
    src/mon_info.c
    src/monk.c
    src/monspell.c
    src/monster.c
    src/monster1.c
    src/monster2.c
    src/mspells1.c
    src/mspells2.c
    src/mut.c
    src/mut_a.c
    src/mystic.c
    src/necromancer.c
    src/ninja.c
    src/ninjalaw.c
    src/obj.c
    src/obj_info.c
    src/obj_prompt.c
    src/object1.c
    src/object2.c
    src/object3.c
    src/obj_kind.c
    src/pack.c
    src/paladin.c
    src/personality.c
    src/polly.c
    src/priest.c
    src/psion.c
    src/py_birth.c
    src/py_info.c
    src/py_screen.c
    src/py_throw.c
    src/quest.c
    src/quiver.c
    src/races.c
    src/races_a.c
    src/races_k.c
    src/races_mf.c
    src/r_angel.c
    src/r_centip.c
    src/r_demon.c
    src/r_dragon.c
    src/r_elem.c
    src/r_eye.c
    src/r_giant.c
    src/r_golem.c
    src/r_hound.c
    src/r_hydra.c
    src/r_jelly.c
    src/r_leprec.c
    src/r_lich.c
    src/r_mimic.c
    src/r_mummy.c
    src/r_orc.c
    src/r_poss.c
    src/r_pump.c
    src/r_quyl.c
    src/r_rag.c
    src/r_spider.c
    src/r_ring.c
    src/r_sword.c
    src/r_troll.c
    src/r_vamp.c
    src/r_vortex.c
    src/r_grazer.c
    src/racial.c
    src/ragemage.c
    src/randname.c
    src/ranger.c
    src/rect.c
    src/red_mage.c
    src/resist.c
    src/rogue.c
    src/rooms.c
    src/rune_kt.c
    src/samurai.c
    src/save.c
    src/savefile.c
    src/scores.c
    src/scout.c
    src/shop.c
    src/skillmaster.c
    src/skills.c
    src/sniper.c
    src/sorcerer.c
    src/spells.c
    src/spells1.c
    src/spells2.c
    src/spells3.c
    src/spells_a.c
    src/spells_c.c
    src/spells_h.c
    src/spells_m.c
    src/spells_s.c
    src/spoilers.c
    src/str-map.c
    src/streams.c
    src/start_screen.c
    src/tables.c
    src/theme.c
    src/chronomancer.c
    src/ui_event.c
    src/ui_death.c
    src/ui_graveyard.c
    src/ui_map.c
    src/ui_menus.c
    src/ui_screen.c
    src/ui_terrain.c
    src/tourist.c
    src/troika.c
    src/util.c
    src/variable.c
    src/variant.c
    src/warlock.c
    src/warrior.c
    src/warrior_mage.c
    src/weaponmaster.c
    src/weaponsmith.c
    src/werewolf.c
    src/wild.c
    src/wild_realm.c
    src/wild_talent.c
    src/wizard1.c
    src/wizard2.c
    src/wiz_obj.c
    src/xtra1.c
    src/xtra2.c
    src/yellow_mage.c
    src/yeqrezh.c
)

# ------------------------------------------------------------------ frontend
# Which display module to build. Each frontend supplies its own entry point,
# so main.c is never compiled -- it would duplicate one.
#
#   win32 : upstream's Win32 GDI port (main-win.c). The known-good baseline.
#   sdl3  : our SDL3 port (main-sdl3.c).
#   none  : no display at all (main-none.c), for playing the game without
#           watching it. See the file for why.
#
#   cmake -S . -B out -D FROG_FRONTEND=sdl3
set(FROG_FRONTEND "win32" CACHE STRING "Display module: win32, sdl3 or none")
set_property(CACHE FROG_FRONTEND PROPERTY STRINGS win32 sdl3 none)

if(FROG_FRONTEND STREQUAL "win32")
    set(FRONTEND_SOURCES src/main-win.c src/readdib.c src/ormeq.rc)
    # WIN32 = windowed subsystem; main-win.c supplies WinMain.
    set(FRONTEND_EXE_TYPE WIN32)
elseif(FROG_FRONTEND STREQUAL "sdl3")
    # ui_art.c is frontend-private: it needs SDL_ttf to measure the font it is
    # about to draw with, so it belongs to the port rather than to the game.
    set(FRONTEND_SOURCES src/main-sdl3.c src/sdl3_map.c src/sdl3_anim.c
                         src/sdl3_look.c src/sdl3_marks.c src/sdl3_calc.c
                         src/sdl3_atlas.c
                         src/ui_art.c src/ui_sound.c src/ui_music.c src/ui_ambient.c
                         src/ui_tiles.c
                         # The application icon. Explorer takes it from
                         # here and so does SDL, for the title bar.
                         src/ormeq_icon.rc)
    # Deliberately a console app for now: SDL and plog diagnostics go to the
    # terminal, which is worth an extra window while the port is young.
    set(FRONTEND_EXE_TYPE "")
elseif(FROG_FRONTEND STREQUAL "none")
    # No display, no SDL, no window. A console program that plays the
    # game from a keystroke script and prints where it got to.
    set(FRONTEND_SOURCES src/main-none.c src/test_melee.c src/test_reckoner.c src/test_audio_events.c src/test_map.c src/test_quest.c)
    set(FRONTEND_EXE_TYPE "")
else()
    message(FATAL_ERROR "FROG_FRONTEND must be win32, sdl3 or none, got '${FROG_FRONTEND}'")
endif()

add_executable(ormeq ${FRONTEND_EXE_TYPE} ${ANGBAND_SOURCES} ${FRONTEND_SOURCES})
if(FROG_FRONTEND STREQUAL "none")
    target_compile_definitions(ormeq PRIVATE RECKONER_TEST ORMEQ_AUDIO_TEST ORMEQ_QUEST_TEST)
endif()

target_include_directories(ormeq PRIVATE src)

target_compile_definitions(ormeq PRIVATE
    WINDOWS
    WIN32
    _CRT_SECURE_NO_WARNINGS      # the codebase is 1990s C; strcpy et al are fine here
    _CRT_NONSTDC_NO_DEPRECATE    # unlink/strdup/etc under their POSIX names
)

# Wizard/debug commands (Ctrl-A in game). Off by default in this build,
# because it lets you wish for items and walk through walls -- but testing a
# fireball without it means playing until you find one.
#
#   .uild.ps1 -Wizard        or        cmake ... -D FROG_WIZARD=ON
option(FROG_WIZARD "Enable wizard/debug commands" OFF)
if(FROG_WIZARD)
    target_compile_definitions(ormeq PRIVATE ALLOW_WIZARD)
else()
    target_compile_definitions(ormeq PRIVATE DISABLE_WIZARD)
endif()

target_link_libraries(ormeq PRIVATE winmm)

if(FROG_FRONTEND STREQUAL "sdl3")
    # Require the reviewed, vendored dependencies. A missing export must fail
    # rather than silently borrowing a neighbouring checkout or installed SDK.
    foreach(package SDL3 SDL3_ttf SDL3_image)
        set(${package}_DIR "${CMAKE_SOURCE_DIR}/third-party/${package}/cmake")
        if(NOT EXISTS "${${package}_DIR}/${package}Config.cmake")
            message(FATAL_ERROR "Missing vendored dependency: ${package}")
        endif()
    endforeach()
    find_package(SDL3 CONFIG REQUIRED)
    find_package(SDL3_ttf CONFIG REQUIRED)
    # Portraits are PNGs. SDL3_image decodes PNG without any of its optional
    # backends, so this adds one DLL and no other dependency.
    find_package(SDL3_image CONFIG REQUIRED)

    target_compile_definitions(ormeq PRIVATE USE_SDL3)
    target_link_libraries(ormeq PRIVATE
        SDL3::SDL3 SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image)

    # Tests use generated PCM fixtures and SDL's dummy device; no sound pack
    # or real speakers are required in a public checkout or CI.
    add_executable(ormeq-tile-tests src/test_tiles.c src/z-rand.c)
    target_include_directories(ormeq-tile-tests PRIVATE src)
    target_compile_definitions(ormeq-tile-tests PRIVATE WINDOWS WIN32 _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(ormeq-tile-tests PRIVATE SDL3::SDL3 SDL3_image::SDL3_image)
    add_custom_command(TARGET ormeq-tile-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:ormeq-tile-tests>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3_image::SDL3_image> $<TARGET_FILE_DIR:ormeq-tile-tests> VERBATIM)

    add_executable(ormeq-sound-tests src/test_sound.c src/z-rand.c)
    target_include_directories(ormeq-sound-tests PRIVATE src)
    target_compile_definitions(ormeq-sound-tests PRIVATE WINDOWS WIN32
        _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    target_link_libraries(ormeq-sound-tests PRIVATE SDL3::SDL3)
    if(MSVC)
        target_compile_options(ormeq-sound-tests PRIVATE /W3 /wd4996 /wd4244 /wd4267)
    endif()
    add_custom_command(TARGET ormeq-sound-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3>
                $<TARGET_FILE_DIR:ormeq-sound-tests> VERBATIM)

    add_executable(ormeq-music-tests src/test_music.c src/z-rand.c)
    target_include_directories(ormeq-music-tests PRIVATE src)
    target_compile_definitions(ormeq-music-tests PRIVATE WINDOWS WIN32
        _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    target_link_libraries(ormeq-music-tests PRIVATE SDL3::SDL3)
    if(MSVC)
        target_compile_options(ormeq-music-tests PRIVATE /W3 /wd4996 /wd4244 /wd4267)
    endif()
    add_custom_command(TARGET ormeq-music-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3>
                $<TARGET_FILE_DIR:ormeq-music-tests> VERBATIM)

    add_executable(ormeq-ambient-tests src/test_ambient.c src/z-rand.c)
    target_include_directories(ormeq-ambient-tests PRIVATE src)
    target_compile_definitions(ormeq-ambient-tests PRIVATE WINDOWS WIN32
        _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    target_link_libraries(ormeq-ambient-tests PRIVATE SDL3::SDL3)
    if(MSVC)
        target_compile_options(ormeq-ambient-tests PRIVATE /W3 /wd4996 /wd4244 /wd4267)
    endif()
    add_custom_command(TARGET ormeq-ambient-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3>
                $<TARGET_FILE_DIR:ormeq-ambient-tests> VERBATIM)

    # The DLLs have to sit next to the executable, which lives in the repo root.
    add_custom_command(TARGET ormeq POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE:SDL3_ttf::SDL3_ttf>
                $<TARGET_FILE:SDL3_image::SDL3_image>
                "${CMAKE_SOURCE_DIR}"
        VERBATIM
    )
endif()

if(MSVC)
    #
    # Warnings on, and the build silent.
    #
    # It was at the default /W1 -- near silent, not because the code is clean
    # but because nobody was asking. At /W3 the whole tree produces about a
    # hundred and seventy, and only eight of them were worth anything: two
    # functions called before they were declared, three dead locals, a mkdir
    # the compiler had never seen, a signed comparison against an unsigned,
    # and a va_copy redefined over the real one. All eight are fixed.
    #
    # The rest are C4244 and C4267 -- narrowing conversions, six dozen of
    # them, and they are what 1990s C looks like when a modern compiler reads
    # it: s16b taking an int, an int taking a size_t. Individually harmless
    # and endemic. Waived deliberately, because a build that prints a hundred
    # and sixty warnings is a build in which the hundred and sixty-first goes
    # unnoticed, and that one might matter.
    #
    # /wd4996 duplicates the _CRT_* defines above but covers a few stragglers.
    #
    target_compile_options(ormeq PRIVATE /W3 /wd4996 /wd4244 /wd4267)

    # ERROR_SUCCESS is ours in h-type.h and Windows' in winerror.h, and both
    # of us say zero. h-type.h is read first, so the guard there never fires
    # and it is windows.h doing the redefining. A collision between two names
    # that agree is not worth renaming an error code the whole tree uses.
    if(FROG_FRONTEND STREQUAL "win32")
        target_compile_options(ormeq PRIVATE /wd4005)
    endif()
endif()

# Distinct name per frontend, so building one never clobbers another.
option(ORMEQ_STACK_AUDIT "Generate MSVC assembly for stack-frame regression checks" OFF)
if(ORMEQ_STACK_AUDIT AND CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/frames")
    target_compile_options(ormeq PRIVATE /FAs "/Fa${CMAKE_BINARY_DIR}/frames/")
endif()
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    foreach(source IN LISTS ANGBAND_SOURCES)
        # The inherited save decoder has a fixed 64 KiB input buffer.
        if(NOT source STREQUAL "src/load.c")
            set_property(SOURCE ${source} APPEND PROPERTY COMPILE_OPTIONS
                -Wframe-larger-than=65536 -Werror=frame-larger-than)
        endif()
    endforeach()
endif()

if(FROG_FRONTEND STREQUAL "sdl3")
    set_target_properties(ormeq PROPERTIES OUTPUT_NAME ormeq-sdl3)
elseif(FROG_FRONTEND STREQUAL "none")
    set_target_properties(ormeq PROPERTIES OUTPUT_NAME ormeq-none)
endif()

# Put the binary next to lib/ (see comment at top).
set_target_properties(ormeq PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}"
)
