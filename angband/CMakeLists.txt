cmake_minimum_required(VERSION 3.20)
project(angband C ASM)

set(CMAKE_C_STANDARD 11)

include_directories(.)


set(SOURCES
        cave.c
        cave-map.c
        cave-square.c
        cave-view.c
        cmd-cave.c
        cmd-core.c
        cmd-misc.c
        cmd-obj.c
        cmd-pickup.c
        debug.c
        effects.c
        game-event.c
        game-input.c
        game-world.c
        generate.c
        gen-cave.c
        gen-chunk.c
        gen-monster.c
        gen-room.c
        gen-util.c
        grafmode.c
        guid.c
        init.c
        load.c
        message.c
        mon-attack.c
        mon-blow-effects.c
        mon-blow-methods.c
        mon-desc.c
        mon-init.c
        mon-list.c
        mon-lore.c
        mon-make.c
        mon-move.c
        mon-msg.c
        mon-power.c
        mon-spell.c
        mon-summon.c
        mon-timed.c
        mon-util.c
        obj-chest.c
        obj-desc.c
        obj-gear.c
        obj-identify.c
        obj-ignore.c
        obj-info.c
        obj-list.c
        obj-make.c
        obj-pile.c
        obj-power.c
        obj-properties.c
        obj-randart.c
        obj-slays.c
        obj-tval.c
        obj-util.c
        option.c
        parser.c
        randname.c
        player-attack.c
        player-birth.c
        player-calcs.c
        player-class.c
        player-history.c
        player-path.c
        player-quest.c
        player-race.c
        player-spell.c
        player-timed.c
        player-util.c
        player.c
        project.c
        project-feat.c
        project-mon.c
        project-obj.c
        project-player.c
        score.c
        save.c
        savefile.c
        store.c
        target.c
        trap.c
        ui-birth.c
        ui-command.c
        ui-context.c
        ui-death.c
        ui-display.c
        ui-event.c
        ui-game.c
        ui-help.c
        ui-history.c
        ui-init.c
        ui-input.c
        ui-keymap.c
        ui-knowledge.c
        ui-map.c
        ui-menu.c
        ui-mon-list.c
        ui-mon-lore.c
        ui-obj-list.c
        ui-object.c
        ui-options.c
        ui-output.c
        ui-player.c
        ui-prefs.c
        ui-score.c
        ui-signals.c
        ui-spell.c
        ui-store.c
        ui-target.c
        ui-term.c
        wiz-debug.c
        wiz-spoil.c
        wiz-stats.c
        buildid.c
        z-bitflag.c
        z-color.c
        z-dice.c
        z-expression.c
        z-file.c
        z-form.c
        z-quark.c
        z-queue.c
        z-rand.c
        z-set.c
        z-textblock.c
        z-type.c
        z-util.c
        z-virt.c
        main.c
        main-stats.c
        alloc.h
        angband.h
        buildid.h
        cave.h
        cmd-core.h
        cmds.h
        config.h
        debug.h
        effects.h
        game-event.h
        game-input.h
        game-world.h
        generate.h
        get.h
        grafmode.h
        guid.h
        h-basic.h
        hint.h
        init.h
        list-blow-effects.h
        list-blow-methods.h
        list-dun-profiles.h
        list-effects.h
        list-elements.h
        list-equip-slots.h
        list-history-types.h
        list-identify-flags.h
        list-ignore-types.h
        list-kind-flags.h
        list-magic-realms.h
        list-message.h
        list-mon-message.h
        list-mon-race-flags.h
        list-mon-spells.h
        list-mon-temp-flags.h
        list-mon-timed.h
        list-object-flags.h
        list-object-modifiers.h
        list-options.h
        list-origins.h
        list-parser-errors.h
        list-player-flags.h
        list-player-timed.h
        list-project-environs.h
        list-project-monsters.h
        list-rooms.h
        list-square-flags.h
        list-stats.h
        list-summon-types.h
        list-terrain-flags.h
        list-trap-flags.h
        list-tvals.h
        main.h
        message.h
        mon-attack.h
        mon-blow-effects.h
        mon-blow-methods.h
        mon-desc.h
        mon-init.h
        mon-list.h
        mon-lore.h
        mon-make.h
        mon-move.h
        mon-msg.h
        mon-power.h
        mon-spell.h
        monster.h
        mon-summon.h
        mon-timed.h
        mon-util.h
        obj-chest.h
        obj-desc.h
        object.h
        obj-gear.h
        obj-identify.h
        obj-ignore.h
        obj-info.h
        obj-list.h
        obj-make.h
        obj-pile.h
        obj-power.h
        obj-properties.h
        obj-randart.h
        obj-slays.h
        obj-tval.h
        obj-util.h
        option.h
        parser.h
        player-attack.h
        player-birth.h
        player-calcs.h
        player.h
        player-history.h
        player-path.h
        player-quest.h
        player-spell.h
        player-timed.h
        player-util.h
        project.h
        randname.h
        savefile.h
        score.h
        store.h
        target.h
        trap.h
        ui-birth.h
        ui-command.h
        ui-context.h
        ui-death.h
        ui-display.h
        ui-event.h
        ui-game.h
        ui-help.h
        ui-history.h
        ui-init.h
        ui-input.h
        ui-keymap.h
        ui-knowledge.h
        ui-map.h
        ui-menu.h
        ui-mon-list.h
        ui-mon-lore.h
        ui-object.h
        ui-obj-list.h
        ui-options.h
        ui-output.h
        ui-player.h
        ui-prefs.h
        ui-score.h
        ui-signals.h
        ui-spell.h
        ui-store.h
        ui-target.h
        ui-term.h
        wizard.h
        z-bitflag.h
        z-color.h
        z-debug.h
        z-dice.h
        z-expression.h
        z-file.h
        z-form.h
        z-quark.h
        z-queue.h
        z-rand.h
        z-set.h
        z-textblock.h
        z-type.h
        z-util.h
        z-virt.h)

if(CMAKE_C_COMPILER MATCHES "m68k-amigaos-gcc$")
    get_filename_component(VASM ${CMAKE_C_COMPILER} DIRECTORY)
    #message(${VASM} " ${CMAKE_C_COMPILER} PATH=$ENV{PATH}")
    set(CMAKE_ASM_COMPILER ${VASM}/vasmm68k_mot)
    set(CMAKE_ASM_FLAGS "-esc -nosym -kick1hunks -Fhunk")
    set(CMAKE_ASM_COMPILE_OBJECT "<CMAKE_ASM_COMPILER> ${CMAKE_ASM_FLAGS} -o <OBJECT> <SOURCE>")
    set(SOURCES ${SOURCES} main-ami.c sound-ami.c sound-ami.h isqrt.asm)
    set(CMAKE_C_FLAGS_MINSIZEREL "-noixemul -DUSE_AMI -s ${CMAKE_C_FLAGS_MINSIZEREL}")
    #set(CMAKE_C_FLAGS_MINSIZEREL "-noixemul -DUSE_AMI -s -DNDEBUG -O3 -m68020")
    message("${CMAKE_C_FLAGS_MINSIZEREL}")
    add_executable(angband ${SOURCES})
    #target_link_libraries(angband m)
    add_custom_command(TARGET angband POST_BUILD
            COMMAND "ls" -l angband
            COMMAND "cp" angband "/home/alex/Documents/FS-UAE/Hard\ Drives/AmigaHD/t/asm/ang/angbandnew"
            DEPENDS angband)

else()
    set(SOURCES ${SOURCES} main-gcu.c snd-sdl.c)
    set(CMAKE_C_FLAGS_DEBUG "-DUSE_GCU -DUSE_NCURSES -fcommon -fsanitize=address ${CMAKE_C_FLAGS_DEBUG}")
    message("${CMAKE_C_FLAGS_DEBUG}")
    add_executable(angband ${SOURCES})
    target_link_libraries(angband m ncursesw)
endif()





