// $Id$
/*
 * Header file for trigger layer routines
 *
 * ==========================================================================
 *
 *  Copyright (C) 2008 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 * 
 * ==========================================================================
 */

#ifndef _SEQ_TRG_H
#define _SEQ_TRG_H


/////////////////////////////////////////////////////////////////////////////
// Global definitions
/////////////////////////////////////////////////////////////////////////////

// reserved memory for each track:
#define SEQ_TRG_MAX_BYTES   256
// each byte holds triggers for 8 steps
// example configurations:
//   - 256 steps, 8 trigger layers: 8*256/8 = 256
//   - 64 steps for 16 drums, 2 trigger layers: 16*64*2/8 = 256
// don't change this value - it directly affects the constraints of the bank file format!

// number of trigger assignments (must be alligned with seq_trg_assignments_t)
#define SEQ_TRG_ASG_NUM 9

enum {
  SEQ_TRG_ASG_GATE,
  SEQ_TRG_ASG_ACCENT,
  SEQ_TRG_ASG_ROLL,
  SEQ_TRG_ASG_GLIDE,
  SEQ_TRG_ASG_SKIP,
  SEQ_TRG_ASG_RANDOM_GATE,
  SEQ_TRG_ASG_RANDOM_VALUE,
  SEQ_TRG_ASG_NO_FX,
  SEQ_TRG_ASG_ROLL_GATE
};

/////////////////////////////////////////////////////////////////////////////
// Global Types
/////////////////////////////////////////////////////////////////////////////

typedef struct {
  u8 packed[(SEQ_TRG_ASG_NUM + 1) / 2];
} seq_trg_assignments_t;

typedef char seq_trg_assignments_must_be_five_bytes[
  sizeof(seq_trg_assignments_t) == 5 ? 1 : -1];

// Trigger assignments are stored as explicit nibbles. Avoiding C bitfields
// makes the five-byte representation independent of compiler allocation order.
static inline u8 SEQ_TRG_AssignmentValueGet(const seq_trg_assignments_t *assignments,
					     u8 trg_num)
{
  if( trg_num >= SEQ_TRG_ASG_NUM )
    return 0;

  return (assignments->packed[trg_num >> 1] >> ((trg_num & 1) << 2)) & 0x0f;
}


static inline void SEQ_TRG_AssignmentValueSet(seq_trg_assignments_t *assignments,
					       u8 trg_num,
					       u8 value)
{
  if( trg_num < SEQ_TRG_ASG_NUM ) {
    u8 shift = (trg_num & 1) << 2;
    u8 mask = 0x0f << shift;
    assignments->packed[trg_num >> 1] =
      (assignments->packed[trg_num >> 1] & ~mask) | ((value & 0x0f) << shift);
  }
}


/////////////////////////////////////////////////////////////////////////////
// Prototypes
/////////////////////////////////////////////////////////////////////////////

extern s32 SEQ_TRG_Init(u32 mode);

extern s32 SEQ_TRG_TrackInit(u8 track, u16 steps, u8 trg_layers, u8 instruments);

extern s32 SEQ_TRG_NumInstrumentsGet(u8 track);
extern s32 SEQ_TRG_NumLayersGet(u8 track);
extern s32 SEQ_TRG_NumStepsGet(u8 track);

extern s32 SEQ_TRG_AssignmentGet(u8 track, u8 trg_num);

extern s32 SEQ_TRG_Get(u8 track, u16 step, u8 trg_layer, u8 trg_instrument);
extern s32 SEQ_TRG_Get8(u8 track, u8 step8, u8 trg_layer, u8 trg_instrument);
extern s32 SEQ_TRG_Get16(u8 track, u8 step16, u8 trg_layer, u8 trg_instrument);

extern s32 SEQ_TRG_GateGet(u8 track, u16 step, u8 trg_instrument);
extern s32 SEQ_TRG_AccentGet(u8 track, u16 step, u8 trg_instrument);
extern s32 SEQ_TRG_RollGet(u8 track, u16 step, u8 trg_instrument);
extern s32 SEQ_TRG_GlideGet(u8 track, u16 step, u8 trg_instrument);
extern s32 SEQ_TRG_SkipGet(u8 track, u16 step, u8 trg_instrument);
extern s32 SEQ_TRG_RandomGateGet(u8 track, u16 step, u8 trg_instrument);
extern s32 SEQ_TRG_RandomValueGet(u8 track, u16 step, u8 trg_instrument);
extern s32 SEQ_TRG_NoFxGet(u8 track, u16 step, u8 trg_instrument);
extern s32 SEQ_TRG_RollGateGet(u8 track, u16 step, u8 trg_instrument);

extern s32 SEQ_TRG_Set(u8 track, u16 step, u8 trg_layer, u8 trg_instrument, u8 value);
extern s32 SEQ_TRG_Set8(u8 track, u8 step8, u8 trg_layer, u8 trg_instrument, u8 value);
extern s32 SEQ_TRG_GateSet(u8 track, u16 step, u8 trg_instrument, u8 value);
extern s32 SEQ_TRG_AccentSet(u8 track, u16 step, u8 trg_instrument, u8 value);
extern s32 SEQ_TRG_RollSet(u8 track, u16 step, u8 trg_instrument, u8 value);
extern s32 SEQ_TRG_GlideSet(u8 track, u16 step, u8 trg_instrument, u8 value);
extern s32 SEQ_TRG_SkipSet(u8 track, u16 step, u8 trg_instrument, u8 value);
extern s32 SEQ_TRG_RandomGateSet(u8 track, u16 step, u8 trg_instrument, u8 value);
extern s32 SEQ_TRG_RandomValueSet(u8 track, u16 step, u8 trg_instrument, u8 value);
extern s32 SEQ_TRG_NoFxSet(u8 track, u16 step, u8 trg_instrument, u8 value);
extern s32 SEQ_TRG_RollGateSet(u8 track, u16 step, u8 trg_instrument, u8 value);

extern char *SEQ_TRG_TypeStr(u8 trg_num);
extern char *SEQ_TRG_AssignedTypeStr(u8 track, u8 trg_layer);


/////////////////////////////////////////////////////////////////////////////
// Export global variables
/////////////////////////////////////////////////////////////////////////////

// should only be directly accessed by SEQ_FILE_B, remaining functions should
// use SEQ_TRG_Get/Set
extern u8 seq_trg_layer_value[SEQ_CORE_NUM_TRACKS][SEQ_TRG_MAX_BYTES];


#endif /* _SEQ_TRG_H */
