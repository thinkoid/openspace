// -*- mode: c++; -*-

#ifndef FREESPACE2_OBJECT_OBJECT_HH
#define FREESPACE2_OBJECT_OBJECT_HH

#include "defs.hh"

#include <functional>

#include "math/vecmat.hh"
#include "object/object_flags.hh"
#include "physics/physics.hh"
#include "util/event.hh"

/*
 *              CONSTANTS
 */

#define DEFAULT_SHIELD_SECTIONS 4 // Number of sections in standard shields.

#ifndef NDEBUG
#define OBJECT_CHECK
#endif

// Object types
#define OBJ_NONE     0 // unused object
#define OBJ_SHIP     1 // a ship
#define OBJ_WEAPON   2 // a laser, missile, etc
#define OBJ_FIREBALL 3 // an explosion
#define OBJ_START    4 // a starting point marker (player start, etc)
#define OBJ_WAYPOINT 5 // a waypoint object, maybe only ever used by Fred
#define OBJ_DEBRIS   6 // a flying piece of ship debris
//#define OBJ_CMEASURE                  7               //a countermeasure, such as chaff
#define OBJ_GHOST     8  // so far, just a placeholder for when a player dies.
#define OBJ_POINT     9  // generic object type to display a point in Fred.
#define OBJ_SHOCKWAVE 10 // a shockwave
#define OBJ_WING      11 // not really a type used anywhere, but I need it for Fred.
#define OBJ_OBSERVER \
        12 // used for multiplayer observers (possibly single player later)
#define OBJ_ASTEROID \
        13               // An asteroid, you know, a big rock, like debris, sort of.
#define OBJ_JUMP_NODE 14 // A jump node object, used only in Fred.
#define OBJ_BEAM                                                                        \
        15      // beam weapons. we have to roll them into the object system to get the
                // benefits of the collision pairs

// Make sure to change Object_type_names in Object.c when adding another type!
#define MAX_OBJECT_TYPES 16

#define UNUSED_OBJNUM \
        (-MAX_OBJECTS * 2) // Newer systems use this instead of -1 for invalid object.

extern const char *Object_type_names[MAX_OBJECT_TYPES];

// each object type should have these functions:  (I will use weapon as
// example)
//
// int weapon_create( weapon specific parameters )
// {
// ...
// objnum = obj_create();
// ... Do some check to correctly handle obj_create returning  which
// means that that object couldn't be created
// ... Initialize the weapon-specific info in Objects[objnum]
// return objnum;
// }
//
// void weapon_delete( object * obj )
// {
// {Put a call to this in OBJECT.C, function
// obj_delete_all_that_should_be_dead } WARNING: To kill an object, set it's
// OF_SHOULD_BE_DEAD flag.  Then, this function will get called when it's
// time to clean up the data. Assert(
// obj->flags[Object::Object_Flags::Should_be_dead] );
// ...
// ... Free up all weapon-specfic data
// obj_delete(objnum);
// }
//
// void weapon_move( object * obj )
// {
// {Put a call to this in ??? }
// ... Do whatever needs to be done each frame.  Usually this amounts
// to setting the thrust, seeing if we've died, etc.
// }
//
// int weapon_check_collision( object * obj, object * other_obj, vec3d * hitpos
// )
// {
// this should check if a vector from
// other_obj->last_pos to other_obj->pos with a radius of
// other_obj->radius
// collides with object obj.   If it does, then fill in hitpos with the
// point of impact and return non-zero, otherwise return 0 if no impact.
// Note that this shouldn't take any action... that happens in weapon_hit.
// }

//
// void weapon_hit( object * obj, object * other_obj, vec3d * hitpos )
// {
// {Put a call to this in COLLIDE.C}
// ... Do what needs to be done when this object gets hit
// ... Reducing shields, etc
// }

struct obj_flag_name {
        Object::Object_Flags flag;
        char flag_name[TOKEN_LENGTH];
        int flag_list;
};

#define MAX_OBJECT_FLAG_NAMES 10
extern obj_flag_name Object_flag_names[];

struct dock_instance;
class model_draw_list;

class object {
public:
        class object *next, *prev; // for linked lists of objects
        int signature;             // Every object ever has a unique signature...
        char type;                 // what type of object this is... robot, weapon, hostage,
                                   // powerup, fireball
        int parent;                // This object's parent.
        int parent_sig;            // This object's parent's signature
        char parent_type;          // This object's parent's type
        int instance;              // which instance.  ie.. if type is Robot, then this indexes
                                   // into the Robots array
        flagset< Object::Object_Flags >
                flags;                        // misc flags.  Call obj_set_flags to change this.
        vec3d pos;                            // absolute x,y,z coordinate of center of object
        matrix orient;                        // orientation of object in world
        float radius;                         // 3d size of object - for collision detection
        vec3d last_pos;                       // where object was last frame
        matrix last_orient;                   // how the object was oriented last frame
        physics_info phys_info;               // a physics object
        int n_quadrants;                      // how many shield quadrants the ship has
        std::vector< float > shield_quadrant; // Shield is broken into
                                              // components, quadrants by default.
        float hull_strength;                  // Remaining hull strength.
        float sim_hull_strength;              // Simulated hull strength - used with training
                                              // weapons.
        std::vector< int > objsnd_num;        // Index of persistant sound struct.

        int num_pairs; // How many object pairs this is associated with.  When 0
                       // then there are no more.

        dock_instance *dock_list; // Goober5000 - objects this object is docked to
        dock_instance *
                dead_dock_list; // Goober5000 - objects this object was docked to when
                                // destroyed; replaces dock_objnum_when_dead

        int collision_group_id; // This is a bitfield. Collision checks will be
                                // skipped if A->collision_group_id &
                                // B->collision_group_id returns nonzero

        util::event< void, object * > pre_move_event;
        util::event< void, object * > post_move_event;

        object();
        ~object();
        void clear();

private:
        // An object should never be copied; there are allocated pointers, and
        // linked list shenanigans.
        object(const object &other);            // no implementation
        object &operator=(const object &other); // no implementation
};

struct object_h {
        object *objp;
        int sig;

        bool IsValid() const
        {
                return (objp != NULL && objp->signature == sig && sig > 0);
        }
        object_h(object *in)
        {
                objp = in;
                if (objp != NULL) {
                        sig = in->signature;
                }
        }
        object_h()
        {
                objp = NULL;
                sig = -1;
        }
};

// object backup struct used by Fred.
struct object_orient_pos {
        vec3d pos;
        matrix orient;
};

#ifdef OBJECT_CHECK
class checkobject {
public:
        int type;
        int signature;
        flagset< Object::Object_Flags > flags;
        int parent_sig;
        int parent_type;

        checkobject();
};
#endif

/*
 *              VARIABLES
 */

extern int Object_inited;
extern int Show_waypoints;

// The next signature for the next newly created object. Zero is bogus
extern int Object_next_signature;
extern int Num_objects;

extern object Objects[];
extern int Highest_object_index; // highest objnum
extern int Highest_ever_object_index;
extern object obj_free_list;
extern object obj_used_list;
extern object obj_create_list;

extern int render_total;
extern int render_order[MAX_OBJECTS];

extern object *Viewer_obj; // Which object is the viewer. Can be NULL.
extern object *Player_obj; // Which object is the player. Has to be valid.

// Use this instead of "objp - Objects" to get an object number
// given it's pointer.  This way, we can replace it with a macro
// to check that the pointer is valid for debugging.
// This code will break in 64 bit builds when we have more than 2^31 objects
// but that will probably never happen
#define OBJ_INDEX(objp) (int)(objp - Objects)

/*
 *              FUNCTIONS
 */

// do whatever setup needs to be done
void obj_init();

void obj_shutdown();

// initialize a new object.  adds to the list for the given segment.
// returns the object number.  The object will be a non-rendering, non-physics
// object.  Returns 0 if failed, otherwise object index.
// You can pass 0 for parent if you don't care about that.
// You can pass null for orient and/or pos if you don't care.
int obj_create(
        ubyte type, int parent_obj, int instance, matrix *orient, vec3d *pos,
        float radius, const flagset< Object::Object_Flags > &flags);

void obj_render(object *obj);

void obj_queue_render(object *obj, model_draw_list *scene);

// Sorts and renders all the ojbects
void obj_render_all(
        const std::function< void(object *) > &render_function,
        bool *render_viewer_last);

// move all objects for the current frame
void obj_move_all(float frametime); // moves all objects

// function to delete an object -- should probably only be called directly from
// editor code
void obj_delete(int objnum);

void obj_delete_all();

// should only be used by the editor!
void obj_merge_created_list(void);

// recalculate object pairs for an object
#define OBJ_RECALC_PAIRS(obj_to_reset)                                         \
        do {                                                                   \
                obj_set_flags(                                                 \
                        obj_to_reset,                                          \
                        obj_to_reset->flags - Object::Object_Flags::Collides); \
                obj_set_flags(                                                 \
                        obj_to_reset,                                          \
                        obj_to_reset->flags + Object::Object_Flags::Collides); \
        } while (0);

// Removes any occurances of object 'a' from the pairs list.
void obj_remove_pairs(object *a);

// add an object to the pairs list
void obj_add_pairs(int objnum);

// Returns true if objects A and B are expected to collide in next duration
// seconds.     For purposes of this check, the first object moves from current
// location to predicted        location.  The second object is assumed to be where
// it will be at time duration, NOT     where it currently is.  radius_scale: 0.0f
// means use polygon models, else scale sphere size by radius_scale radius_scale
//== 1.0f means Descent style collisions.
int objects_will_collide(
        object *A, object *B, float duration, float radius_scale);

// Used for pausing.  Seems hacked.  Was in PHYSICS, but that broke the
// TestCode program, so I moved it into the object lib.  -John
void obj_init_all_ships_physics();

// Goober5000
float get_hull_pct(object *objp);
float get_sim_hull_pct(object *objp);
float get_shield_pct(object *objp);

// returns the average 3-space position of all ships.  useful to find "center"
// of battle (sort of)
void obj_get_average_ship_pos(vec3d *pos);

// function to deal with firing player things like lasers, missiles, etc.
// separated out because of multiplayer issues.
void obj_player_fire_stuff(object *objp, control_info ci);

// Call this if you want to change an object flag so that the
// object code knows what's going on.  For instance if you turn
// off OF_COLLIDES, the object code needs to know this in order to
// actually turn the object collision detection off.  By calling
// this you shouldn't get Int3's in the checkobject code.  If you
// do, then put code in here to correctly handle the case.
void obj_set_flags(
        object *obj, const flagset< Object::Object_Flags > &new_flags);

// get the team for any object
int obj_team(object *objp);

void obj_move_all_pre(object *objp, float frametime);
void obj_move_all_post(object *objp, float frametime);

void obj_move_call_physics(object *objp, float frametime);

// multiplayer object update stuff begins
// -------------------------------------------

/**
 * @brief Checks if the given object is docked with anyone.
 *
 * @returns Nonzero if docked, or
 * @returns 0 if not docked
 *
 * @author Goober5000
 */
int object_is_docked(object *objp);

/**
 * @brief Checks if the given object is dead-docked with anyone.
 *
 * @returns Nonzero if docked, or
 * @returns 0 if not docked
 *
 * @details An object is "dead-docked" when it is dying and still has objects
 * docked to it. The dead_dock list is populated when the object dies, and is
 * used later on to jettison and maybe damage the docked objects.
 *
 * @author Goober5000
 */
int object_is_dead_docked(object *objp);

/**
 * @brief Moves a docked object to keep up with the parent object as it moves
 *
 * @param[in,out] objp The docked object
 * @param[in]     parent_objp The object that it's docked to
 *
 * @author Goober5000
 */
void obj_move_one_docked_object(object *objp, object *parent_objp);

// WMC
void object_set_gliding(object *objp, bool enable = true, bool force = false);
bool object_get_gliding(object *objp);
bool object_glide_forced(object *objp);
int obj_get_by_signature(int sig);
int object_get_model(object *objp);

void obj_render_queue_all();

#endif // FREESPACE2_OBJECT_OBJECT_HH