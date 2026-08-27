/* SPDX-License-Identifier: MIT */
#ifndef GXMETAL_DRAW_POLICY_H
#define GXMETAL_DRAW_POLICY_H

#include <stdint.h>

#include "gxmetal_protocol.h"

#define GXMETAL_DRAW_BATCH_NONE UINT32_C(0)
#define GXMETAL_DRAW_BATCH_GOURAUD UINT32_C(1)
#define GXMETAL_DRAW_BATCH_TEXTURE UINT32_C(2)

/* ATI's private OpenGL callbacks submit already transformed homogeneous
 * coordinates.  Only mark that provenance when the negotiated host protocol
 * understands it; older hosts must continue to receive legacy draw flags. */
static inline uint32_t gxmetal_guest_ati_private_draw_flags(
    uint64_t negotiated_features)
{
    return (negotiated_features & GXMETAL_FEATURE_HOMOGENEOUS_DRAW) != 0 ?
        GXMETAL_DRAW_HOMOGENEOUS : GXMETAL_DRAW_NONE;
}

/* All fields describe whole-draw provenance and therefore form part of a
 * batch's identity.  A transition must flush the pending batch instead of
 * applying the newest interpretation to vertices queued under an older one. */
static inline int gxmetal_guest_draw_batch_can_append(
    uint32_t pending_count, uint32_t pending_kind, uint32_t pending_flags,
    uint32_t pending_auxiliary, uint32_t next_kind, uint32_t next_flags,
    uint32_t next_auxiliary)
{
    return pending_count == 0 ||
        (pending_kind == next_kind && pending_flags == next_flags &&
         pending_auxiliary == next_auxiliary);
}

#endif /* GXMETAL_DRAW_POLICY_H */
