// -*- mode: c++; -*-

#include "hud/hudescort.hh"

#include "defs.hh"

#include "gamesnd/gamesnd.hh"
#include "hud/hudmessage.hh"
#include "hud/hudparse.hh"
#include "hud/hudshield.hh"
#include "hud/hudtargetbox.hh"
#include "iff_defs/iff_defs.hh"
#include "io/timer.hh"
#include "log/log.hh"
#include "object/object.hh"
#include "parse/parselo.hh"
#include "playerman/player.hh"
#include "shared/alphacolors.hh"
#include "shared/globals.hh"
#include "ship/ship.hh"
#include "util/list.hh"
#include "weapon/emp.hh"

int Show_escort_view;

typedef struct escort_info {
        int objnum;
        int obj_signature; // so we are sure we have a valid objnum
        int priority;      // higher priority is higher in the list
        short np_id;       // netplayer id (for multiplayer dogfight mode)

        // These parallel the way the shield_hit_info struct works; intentionally
        // not using it here because this is a lot simpler and less error-prone
        int escort_hit_timer;
        int escort_hit_next_flash;
        bool escort_show_bright;
} escort_info;

escort_info Escort_ships[MAX_COMPLETE_ESCORT_LIST];
int Num_escort_ships;
int Max_escort_ships = 3;

// int Escort_gauge_y[MAX_ESCORT_SHIPS] = { 219, 230, 241 };

/*
int Escort_gauge_text_coords[GR_NUM_RESOLUTIONS][MAX_ESCORT_SHIPS][4][2] =
{
    { // GR_640
        {
            {489,219},
            {599,212},
            {604,219},
            {474,219}
        },
        {
            {489,230},
            {599,223},
            {604,230},
            {474,230}
        },
        {
            {489,241},
            {599,234},
            {604,241},
            {474,241}
        },
    },
    { // GR_1024
        {
            {869,343},
            {973,338},
            {981,343},
            {854,343}
        },
        {
            {869,354},
            {973,349},
            {981,354},
            {854,354}
        },
        {
            {869,365},
            {973,360},
            {981,365},
            {854,365}
        },
    }
};

// escort gauge coords
int Escort_coords[GR_NUM_RESOLUTIONS][4][2] = {
    { // GR_640
        {486, 206},
        {486, 219},
        {486, 230},
        {486, 241}
    },
    { // GR_1024
        {865, 330},
        {865, 343},
        {865, 354},
        {865, 365}
    }
};

// monitoring text coords
int Monitoring_coords[GR_NUM_RESOLUTIONS][2] = {
    { // GR_640
        489, 208
    },
    { // GR_1024
        869, 331
    }
};

char *Escort_gauge_filenames[GR_NUM_RESOLUTIONS][MAX_ESCORT_SHIPS] =
{
//XSTR:OFF
    { // GR_640
        "escort1",
        "escort2",
        "escort3"
    },
    { // GR_1024
        "escort1",
        "escort2",
        "escort3"
    }
//XSTR:ON
};*/

static int Last_target_index; // index into Escort_gauges for last targeted via
                              // 'Next Escort Target' key

HudGaugeEscort::HudGaugeEscort()
        : HudGauge(
                HUD_OBJECT_ESCORT, HUD_ESCORT_VIEW, false, false,
                (VM_EXTERNAL | VM_DEAD_VIEW | VM_WARP_CHASE | VM_PADLOCK_ANY | VM_OTHER_SHIP),
                255, 255, 255) { }

void HudGaugeEscort::initHeaderText(char *text)
{
        strcpy(header_text, text);
}

void HudGaugeEscort::initHeaderTextOffsets(int x, int y)
{
        header_text_offsets[0] = x;
        header_text_offsets[1] = y;
}

void HudGaugeEscort::initListStartOffsets(int x, int y)
{
        list_start_offsets[0] = x;
        list_start_offsets[1] = y;
}

void HudGaugeEscort::initEntryHeight(int h) { entry_h = h; }

void HudGaugeEscort::initEntryStaggerWidth(int w) { entry_stagger_w = w; }

void HudGaugeEscort::initBottomBgOffset(int offset)
{
        bottom_bg_offset = offset;
}

void HudGaugeEscort::initShipNameOffsets(int x, int y)
{
        ship_name_offsets[0] = x;
        ship_name_offsets[1] = y;
}

void HudGaugeEscort::initShipIntegrityOffsets(int x, int y)
{
        ship_integrity_offsets[0] = x;
        ship_integrity_offsets[1] = y;
}

void HudGaugeEscort::initShipStatusOffsets(int x, int y)
{
        ship_status_offsets[0] = x;
        ship_status_offsets[1] = y;
}

void HudGaugeEscort::initShipNameMaxWidth(int w) { ship_name_max_width = w; }

void HudGaugeEscort::initRightAlignNames(bool align)
{
        right_align_names = align;
}

void HudGaugeEscort::initBitmaps(
        char *fname_top, char *fname_middle, char *fname_bottom)
{
        Escort_gauges[0].first_frame = bm_load_animation(fname_top, &Escort_gauges[0].num_frames);
        if (Escort_gauges[0].first_frame == -1) {
                WARNINGF(LOCATION, "Could not load in ani: %s", fname_top);
                return;
        }

        Escort_gauges[1].first_frame = bm_load_animation(fname_middle, &Escort_gauges[1].num_frames);
        if (Escort_gauges[1].first_frame == -1) {
                WARNINGF(LOCATION, "Could not load in ani: %s", fname_middle);
                return;
        }

        Escort_gauges[2].first_frame = bm_load_animation(fname_bottom, &Escort_gauges[2].num_frames);
        if (Escort_gauges[2].first_frame == -1) {
                WARNINGF(LOCATION, "Could not load in ani: %s", fname_bottom);
                return;
        }
}

void HudGaugeEscort::pageIn()
{
        int i;

        for (i = 0; i < NUM_ESCORT_FRAMES; i++) {
                bm_page_in_aabitmap(
                        Escort_gauges[i].first_frame, Escort_gauges[i].num_frames);
        }
}

int HudGaugeEscort::setGaugeColorEscort(int index, int team)
{
        int is_flashing = 0;
        int is_bright = 0;
        int seen_from_team = (Player_ship != NULL) ? Player_ship->team : -1;

        // set flashing color
        if (!timestamp_elapsed(Escort_ships[index].escort_hit_timer)) {
                is_flashing = 1;
                if (Escort_ships[index].escort_show_bright) {
                        is_bright = 1;
                }
        }

        // Goober5000 - now base this on team color
        gr_set_color_fast(iff_get_color_by_team_and_object(
                team, seen_from_team, is_bright,
                &Objects[Escort_ships[index].objnum]));

        // Goober5000 - an alternative; same as original but incorporating teams
        // for non-friendlies
        /*
    if ((seen_from_team == team) || (seen_from_team < 0))       // :V: sez assume
    friendly if Player_ship is NULL hud_set_gauge_color(HUD_ESCORT_VIEW,
    is_bright ? HUD_C_BRIGHT : HUD_C_DIM); else
        gr_set_color_fast(iff_get_color_by_team(team, seen_from_team,
    is_bright));
    */

        // Goober5000 - original color logic
        /*
    if ((seen_from_team == team) || (seen_from_team < 0))       // :V: sez assume
    friendly if Player_ship is NULL hud_set_gauge_color(HUD_ESCORT_VIEW,
    is_bright ? HUD_C_BRIGHT : HUD_C_DIM); else gr_set_color_fast(is_bright ?
    &Color_bright_red : &Color_red);
    */

        return is_flashing;
}

void HudGaugeEscort::render(float /*frametime*/)
{
        int i = 0;

        if (!Show_escort_view) {
                return;
        }

        if (!Num_escort_ships) {
                return;
        }

        // hud_set_default_color();
        setGaugeColor();

        // draw the top of the escort view
        renderBitmap(Escort_gauges[0].first_frame, position[0], position[1]);
        renderString(
                position[0] + header_text_offsets[0],
                position[1] + header_text_offsets[1], header_text);

        int x = position[0] + list_start_offsets[0];
        int y = position[1] + list_start_offsets[1];

        // This is temporary
        Num_escort_ships--;
        i = 0;

        if (Num_escort_ships) {
                for (; i < Num_escort_ships; i++) {
                        if (i != 0) {
                                x += entry_stagger_w;
                                y += entry_h;
                        }
                        renderBitmap(Escort_gauges[1].first_frame, x, y);

                        // Now we just show the ships info
                        renderIcon(x, y, i);
                }

                // Increment for last entry
                x += entry_stagger_w;
                y += entry_h;
        }

        // Back to right #
        Num_escort_ships++;

        // Show the last escort entry
        renderBitmap(Escort_gauges[2].first_frame, x, y + bottom_bg_offset);
        renderIcon(x, y, i);
}

// draw the shield icon and integrity for the escort ship
void HudGaugeEscort::renderIcon(int x, int y, int index)
{
        float shields, integrity;
        int screen_integrity, offset;
        char buf[255];

        object *objp = &Objects[Escort_ships[index].objnum];
        ship *sp = &Ships[objp->instance];

        // determine if its "friendly" or not
        // Goober5000 - changed in favor of just passing the team
        setGaugeColorEscort(index, sp->team);
        /*
    if(Player_ship != NULL){
        hud_escort_set_gauge_color(index, (sp->team == Player_ship->team) ? 1 :
    0); } else { hud_escort_set_gauge_color(index, 1);
    }
    */

        // draw a 'D' if a ship is disabled
        if ((sp->flags[Ship::Ship_Flags::Disabled]) || (ship_subsys_disrupted(sp, SUBSYSTEM_ENGINE))) {
                renderString(
                        x + ship_status_offsets[0], y + ship_status_offsets[1], EG_NULL,
                        XSTR("D", 284));
        }

        // print out ship name
        strcpy(buf, sp->get_display_string());
        font::force_fit_string(buf, 255, ship_name_max_width);

        end_string_at_first_hash_symbol(buf);
        const int w = font::force_fit_string(buf, 255, ship_name_max_width);

        if (right_align_names) {
                renderString(
                        x + ship_name_offsets[0] + ship_name_max_width - w,
                        y + ship_name_offsets[1], EG_ESCORT1 + index, buf);
        } else {
                renderString(
                        x + ship_name_offsets[0], y + ship_name_offsets[1],
                        EG_ESCORT1 + index, buf);
        }

        // show ship integrity
        hud_get_target_strength(objp, &shields, &integrity);
        screen_integrity = (int)std::lround(integrity * 100);
        offset = 0;
        if (screen_integrity < 100) {
                offset = 2;
                if (screen_integrity == 0) {
                        if (integrity > 0) {
                                screen_integrity = 1;
                        }
                }
        }
        renderPrintf(
                x + ship_integrity_offsets[0] + offset, y + ship_integrity_offsets[1],
                EG_NULL, "%d", screen_integrity);

        // Let's be nice.
        setGaugeColor();
}

void hud_escort_update_list()
{
        if (Num_escort_ships) {
                for (int i = 0; i < Num_escort_ships; i++) {
                        if (!timestamp_elapsed(Escort_ships[i].escort_hit_timer)) {
                                if (timestamp_elapsed(
                                            Escort_ships[i].escort_hit_next_flash)) {
                                        Escort_ships[i].escort_hit_next_flash = timestamp(SHIELD_FLASH_INTERVAL);
                                        Escort_ships[i].escort_show_bright = !Escort_ships[i]
                                                                                      .escort_show_bright; // toggle between default and
                                                                                                           // bright frames
                                }
                        }
                }
        }
}

// called from HUD init, loads the bitmap data in once, and resets any data for
// each level
void hud_escort_init()
{
        Last_target_index = -1;

        if (Max_escort_ships > MAX_COMPLETE_ESCORT_LIST) {
                Max_escort_ships = MAX_COMPLETE_ESCORT_LIST;
        }
}

// ----------------------------------------------------------------------
// hud_escort_clear_all()
//
void hud_escort_clear_all(bool clear_flags)
{
        int i;

        Num_escort_ships = 0;
        for (i = 0; i < Max_escort_ships; i++) {
                if (clear_flags && (Escort_ships[i].objnum >= 0) && (Objects[Escort_ships[i].objnum].type == OBJ_SHIP) && (Objects[Escort_ships[i].objnum].instance >= 0)) {
                        Ships[Objects[Escort_ships[i].objnum].instance].flags.remove(
                                Ship::Ship_Flags::Escort);
                }
                Escort_ships[i].obj_signature = -99;
                Escort_ships[i].np_id = -1;
                Escort_ships[i].escort_hit_timer = 0;
                Escort_ships[i].escort_hit_next_flash = 0;
                Escort_ships[i].escort_show_bright = false;
        }
}

// internal helper function for sort.
// sorts first by priority number and then alphabetically
int escort_compare_func(const void *e1, const void *e2)
{
        escort_info *escort1, *escort2;
        int diff;
        int ret;

        escort1 = (escort_info *)e1;
        escort2 = (escort_info *)e2;

        diff = escort2->priority - escort1->priority;

        if (diff != 0) {
                ret = diff;
        } else {
                char *name1, *name2;
                name1 = Ships[Objects[escort1->objnum].instance].ship_name;
                name2 = Ships[Objects[escort2->objnum].instance].ship_name;

                ret = strcasecmp(name1, name2);
        }

        return ret;
}

// create complete priority sorted escort list for all active ships
// escorts - array of escort info
// num_escorts - number of escorts requests in field of active ships
// This will be culled to MAX_ESCORTS, selecting the top set from escorts
void hud_create_complete_escort_list(escort_info *escorts, int *num_escorts)
{
        ship_obj *so;
        object *objp;

        // start with none on list
        *num_escorts = 0;

        for (so = GET_FIRST(&Ship_obj_list);
             so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so)) {
                ASSERT(so->objnum >= 0 && so->objnum < MAX_OBJECTS);
                if ((so->objnum < 0) || (so->objnum >= MAX_OBJECTS)) {
                        continue;
                }
                objp = &Objects[so->objnum];
                ASSERT(objp->type == OBJ_SHIP);
                if (objp->type != OBJ_SHIP) {
                        continue;
                }

                // break out of the loop when we have reached our max
                if (*num_escorts == MAX_COMPLETE_ESCORT_LIST) {
                        WARNINGF(LOCATION, "exceeded max ships in big escort list");
                        break;
                }

                // only process ships that might be on the list
                if (!(Ships[objp->instance].flags[Ship::Ship_Flags::Escort])) {
                        continue;
                }

                // only process ships that can be seen by sensors
                if ((Ships[objp->instance]
                             .flags[Ship::Ship_Flags::Hidden_from_sensors])) {
                        continue;
                }

                // don't process most stealth ships
                if ((Ships[objp->instance].flags[Ship::Ship_Flags::Stealth])) {
                        if (Ships[objp->instance].team == Player_ship->team) {
                                // friendly stealths are only not seen when explicitly
                                // specified
                                if (Ships[objp->instance]
                                            .flags[Ship::Ship_Flags::Friendly_stealth_invis]) {
                                        continue;
                                }
                        }
                        // non-friendly stealths are never seen
                        else {
                                continue;
                        }
                }

                // don't process objects that should be dead
                if (objp->flags[Object::Object_Flags::Should_be_dead]) {
                        continue;
                }

                // add the ship
                escorts[*num_escorts].objnum = so->objnum;
                escorts[*num_escorts].obj_signature = objp->signature;
                escorts[*num_escorts].priority = Ships[objp->instance].escort_priority;
                escorts[*num_escorts].np_id = -1;
                escorts[*num_escorts].escort_hit_timer = 0;
                escorts[*num_escorts].escort_hit_next_flash = 0;
                escorts[*num_escorts].escort_show_bright = false;
                (*num_escorts)++;
        }
}

// ----------------------------------------------------------------------
// hud_init_escort_info()
//
// Set up the escort list
//
void hud_setup_escort_list(int level)
{
        int num_escorts, num_complete_escorts;
        escort_info complete_escorts[MAX_COMPLETE_ESCORT_LIST];

        hud_escort_clear_all();

        // get complete escort list
        hud_create_complete_escort_list(complete_escorts, &num_complete_escorts);

        // sort escort list by priority
        insertion_sort(
                complete_escorts, num_complete_escorts, sizeof(escort_info),
                escort_compare_func);

        // set number in escort list
        num_escorts = num_complete_escorts;
        if (num_escorts > Max_escort_ships) {
                num_escorts = Max_escort_ships;
        }

        // add ships to escort list
        for (Num_escort_ships = 0; Num_escort_ships < num_escorts;
             Num_escort_ships++) {
                Escort_ships[Num_escort_ships].obj_signature = complete_escorts[Num_escort_ships].obj_signature;
                Escort_ships[Num_escort_ships].priority = complete_escorts[Num_escort_ships].priority;
                Escort_ships[Num_escort_ships].objnum = complete_escorts[Num_escort_ships].objnum;
                Escort_ships[Num_escort_ships].np_id = complete_escorts[Num_escort_ships].np_id;
                Escort_ships[Num_escort_ships].escort_hit_timer = 0;
                Escort_ships[Num_escort_ships].escort_hit_next_flash = 0;
                Escort_ships[Num_escort_ships].escort_show_bright = false;
        }

        if (level) {
                Show_escort_view = 1;
        }
}

// combine complete escort list with Escort_ships, keeping valid hit info
void merge_escort_lists(
        escort_info * /* complete_escorts */, int num_complete_escorts)
{
        int top_complete_escorts;

        // may be > 1 ship change to list (ie, 2 or 3 culled during same frame)
        // set Num_escort_ships and cap
        Num_escort_ships = num_complete_escorts;
        if (Num_escort_ships > Max_escort_ships) {
                Num_escort_ships = Max_escort_ships;
        }

        // nothing to do
        if (Num_escort_ships == 0) {
                return;
        }

        // get the top slots in complete escort list that will be copied onto
        // Escort_ships
        top_complete_escorts = num_complete_escorts;
        if (top_complete_escorts > Max_escort_ships) {
                top_complete_escorts = Max_escort_ships;
        }

        // reset Num_escort_ships
        Num_escort_ships = top_complete_escorts;
}

// ----------------------------------------------------------------------
// hud_remove_ship_from_escort_index()
//
// Take a ship out of the escort list
void hud_remove_ship_from_escort_index(int dead_index, int objnum)
{
        int i, count, num_complete_escorts;
        escort_info bakup_arr[MAX_COMPLETE_ESCORT_LIST],
                complete_escorts[MAX_COMPLETE_ESCORT_LIST];

        // remove him from escort list
        if ((objnum >= 0) && (Objects[objnum].type == OBJ_SHIP) && (Objects[objnum].instance >= 0)) {
                Ships[Objects[objnum].instance].flags.remove(
                        Ship::Ship_Flags::Escort);
        }

        count = 0;
        for (i = 0; i < Num_escort_ships; i++) {
                if (i != dead_index) {
                        bakup_arr[count++] = Escort_ships[i];
                }
        }

        for (i = 0; i < count; i++) { Escort_ships[i] = bakup_arr[i]; }

        Num_escort_ships--;
        ASSERT(Num_escort_ships >= 0);

        // get complete escort list
        hud_create_complete_escort_list(complete_escorts, &num_complete_escorts);

        // sort escort list by priority
        insertion_sort(
                complete_escorts, num_complete_escorts, sizeof(escort_info),
                escort_compare_func);

        // merge list
        merge_escort_lists(complete_escorts, num_complete_escorts);

        hud_gauge_popup_start(HUD_ESCORT_VIEW);
}

// called once per frame to refresh the escort list if important flags changed
void hud_escort_cull_list()
{
        for (int i = 0; i < Num_escort_ships; i++) {
                int objnum = Escort_ships[i].objnum;
                ASSERT(objnum >= 0 && objnum < MAX_OBJECTS);

                if (Objects[objnum].flags[Object::Object_Flags::Should_be_dead]) {
                        hud_setup_escort_list(0);
                        break;
                } else if (Objects[objnum].type == OBJ_SHIP) {
                        int shipnum = Objects[objnum].instance;
                        ASSERT(shipnum >= 0 && shipnum < MAX_SHIPS);

                        if ((Ships[shipnum]
                                     .flags[Ship::Ship_Flags::Hidden_from_sensors])
                            || ((Ships[shipnum].flags[Ship::Ship_Flags::Stealth]) && ((Ships[shipnum].team != Player_ship->team) || (Ships[shipnum].flags[Ship::Ship_Flags::Friendly_stealth_invis])))) {
                                hud_setup_escort_list(0);
                                break;
                        }
                }
        }
}

// ----------------------------------------------------------------------
// hud_escort_view_toggle()
//
void hud_escort_view_toggle()
{
        Show_escort_view ^= 1;
        if (Show_escort_view) {
                HUD_sourced_printf(
                        HUD_SOURCE_HIDDEN, "%s", XSTR("Escort view enabled", 286));
        } else {
                HUD_sourced_printf(
                        HUD_SOURCE_HIDDEN, "%s", XSTR("Escort view disabled", 287));
        }
}

// try to add a ship to the escort list, if slot available
void hud_add_ship_to_escort(int objnum, int supress_feedback)
{
        escort_info complete_escorts[MAX_COMPLETE_ESCORT_LIST];
        int num_complete_escorts, idx, found;

        // get complete escort list
        hud_create_complete_escort_list(complete_escorts, &num_complete_escorts);

        // ensure the complete escort list is not full already
        if (num_complete_escorts == MAX_COMPLETE_ESCORT_LIST) {
                return;
        }

        // check if ship is already on complete escort list
        found = 0;
        for (idx = 0; idx < num_complete_escorts; idx++) {
                if (complete_escorts[idx].obj_signature == Objects[objnum].signature) {
                        found = 1;
                        break;
                }
        }

        // add new ship into complete list
        if (!found) {
                complete_escorts[num_complete_escorts].objnum = objnum;
                complete_escorts[num_complete_escorts].obj_signature = Objects[objnum].signature;
                complete_escorts[num_complete_escorts].priority = Ships[Objects[objnum].instance].escort_priority;
                complete_escorts[num_complete_escorts].escort_hit_timer = 0;
                complete_escorts[num_complete_escorts].escort_hit_next_flash = 0;
                complete_escorts[num_complete_escorts].escort_show_bright = false;

                // add him to escort list
                Ships[Objects[objnum].instance].flags.set(Ship::Ship_Flags::Escort);

                num_complete_escorts++;
        }

        // sort escort list by priority
        insertion_sort(
                complete_escorts, num_complete_escorts, sizeof(escort_info),
                escort_compare_func);

        // merge list
        merge_escort_lists(complete_escorts, num_complete_escorts);

        // maybe do feedback
        if ((Num_escort_ships == Max_escort_ships) && !supress_feedback) {
                found = 0;
                // search thru list for objnum
                for (idx = 0; idx < Num_escort_ships; idx++) {
                        if (Escort_ships[idx].objnum == objnum) {
                                found = 1;
                                break;
                        }
                }

                if (!found) {
                        HUD_sourced_printf(
                                HUD_SOURCE_HIDDEN,
                                XSTR("Escort list is full with %d ships", 288),
                                Num_escort_ships);
                        snd_play(gamesnd_get_game_sound(GameSounds::TARGET_FAIL));
                }
        }

        hud_gauge_popup_start(HUD_ESCORT_VIEW);
}

// ----------------------------------------------------------------------
// hud_add_remove_ship_escort()
//
void hud_add_remove_ship_escort(int objnum, int supress_feedback)
{
        int in_escort, i;

        if (objnum < 0) {
                ASSERT(0);
                return;
        }

        if (Objects[objnum].type != OBJ_SHIP) {
                if (!supress_feedback) {
                        snd_play(gamesnd_get_game_sound(GameSounds::TARGET_FAIL));
                }
                return;
        }

        in_escort = 0;
        for (i = 0; i < Num_escort_ships; i++) {
                if (Escort_ships[i].obj_signature == Objects[objnum].signature) {
                        in_escort = 1;
                        break;
                }
        }

        if (in_escort) {
                hud_remove_ship_from_escort_index(i, objnum);
                return;
        }

        hud_add_ship_to_escort(objnum, supress_feedback);
}

void hud_remove_ship_from_escort(int objnum)
{
        int in_escort, i;

        if (objnum < 0) {
                ASSERT(0);
                return;
        }

        in_escort = 0;
        for (i = 0; i < Num_escort_ships; i++) {
                if (Escort_ships[i].obj_signature == Objects[objnum].signature) {
                        in_escort = 1;
                        break;
                }
        }

        if (in_escort) {
                hud_remove_ship_from_escort_index(i, objnum);
                return;
        }
}

/**
 * Called whenever a ship is hit to determine if that ship is in the escort
 * list.  If it is, then start timers to flash the name hull/shield icon for
 * that ship.
 *
 * @param objp      The object hit
 * @param quadrant  Shield quadrant on the object that was hit, alternatively
 * -1 if no shield
 */
void hud_escort_ship_hit(object *objp, int /*quadrant*/)
{
        for (int i = 0; i < Num_escort_ships; i++) {
                if (Escort_ships[i].objnum == OBJ_INDEX(objp)) {
                        hud_gauge_popup_start(HUD_ESCORT_VIEW);
                        Escort_ships[i].escort_hit_timer = timestamp(SHIELD_HIT_DURATION);
                        Escort_ships[i].escort_hit_next_flash = timestamp(SHIELD_FLASH_INTERVAL);
                }
        }
}

// target the next ship in the escort list
void hud_escort_target_next()
{
        int objnum;

        if (Num_escort_ships == 0) {
                snd_play(gamesnd_get_game_sound(GameSounds::TARGET_FAIL), 0.0f);
                return;
        }

        Last_target_index++;
        if (Last_target_index >= Num_escort_ships) {
                Last_target_index = 0;
        }

        objnum = Escort_ships[Last_target_index].objnum;
        set_target_objnum(Player_ai, objnum);
        hud_restore_subsystem_target(&Ships[Objects[objnum].instance]);
}

// return the number of ships currently on the escort list
int hud_escort_num_ships_on_list() { return Num_escort_ships; }

// Return the object number for the ship at index position in the escort list
int hud_escort_return_objnum(int index)
{
        int escort_objnum, escort_sig;
        if (index >= Num_escort_ships || index >= MAX_COMPLETE_ESCORT_LIST) {
                return -1;
        }

        escort_objnum = Escort_ships[index].objnum;
        escort_sig = Escort_ships[index].obj_signature;

        if (escort_objnum < 0) {
                return -1;
        }

        // ensure this is still a valid index
        if (Objects[escort_objnum].signature != escort_sig) {
                return -1;
        }

        return Escort_ships[index].objnum;
}