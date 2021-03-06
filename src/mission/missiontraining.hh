// -*- mode: c++; -*-

#ifndef FREESPACE2_MISSION_MISSIONTRAINING_HH
#define FREESPACE2_MISSION_MISSIONTRAINING_HH

#include "defs.hh"

#include "hud/hud.hh"

extern int Max_directives;
extern int Training_message_method;
extern int Training_num_lines;
extern int Training_message_visible;
extern int Training_failure;

void training_mission_init ();
void training_mission_shutdown ();
void training_check_objectives ();
void message_training_queue (const char* text, int timestamp, int length = -1);
void message_training_setup (int num, int length = -1);
void message_translate_tokens (char* buf, const char* text);
void training_fail ();
void message_training_update_frame ();

class HudGaugeDirectives : public HudGauge {
protected:
    hud_frames directives_top;
    hud_frames directives_middle;
    hud_frames directives_bottom;

    int header_offsets[2];
    int middle_frame_offset_y;
    int bottom_bg_offset;
    int text_start_offsets[2];
    int text_h;
    int max_line_width;

public:
    HudGaugeDirectives ();
    void initBitmaps (char* fname_top, char* fname_middle, char* fname_bottom);
    void initHeaderOffsets (int x, int y);
    void initMiddleFrameOffsetY (int y);
    void initBottomBgOffset (int offset);
    void initTextStartOffsets (int x, int y);
    void initTextHeight (int h);
    void initMaxLineWidth (int w);
    void render (float frametime) override;
    void pageIn () override;
    bool canRender () override;
};

class HudGaugeTrainingMessages : public HudGauge {
protected:
public:
    HudGaugeTrainingMessages ();
    void render (float frametime) override;
    void pageIn () override;
    bool canRender () override;
};

#endif // FREESPACE2_MISSION_MISSIONTRAINING_HH
