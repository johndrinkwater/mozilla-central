/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=99 ft=cpp:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "LifoAlloc.h"

using namespace js;

namespace js {
namespace detail {

BumpChunk *
BumpChunk::new_(size_t chunkSize)
{
    JS_ASSERT(RoundUpPow2(chunkSize) == chunkSize);
    void *mem = js_malloc(chunkSize);
    if (!mem)
        return NULL;
    BumpChunk *result = new (mem) BumpChunk(chunkSize - sizeof(BumpChunk));

    // We assume that the alignment of sAlign is less than that of
    // the underlying memory allocator -- creating a new BumpChunk should
    // always satisfy the sAlign alignment constraint.
    JS_ASSERT(AlignPtr(result->bump) == result->bump);
    return result;
}

void
BumpChunk::delete_(BumpChunk *chunk)
{
#ifdef DEBUG
    // Part of the chunk may have been marked as poisoned/noaccess.  Undo that
    // before writing the 0xcd bytes.
    size_t size = sizeof(*chunk) + chunk->bumpSpaceSize;
    MOZ_MAKE_MEM_UNDEFINED(chunk, size);
    memset(chunk, 0xcd, size);
#endif
    js_free(chunk);
}

bool
BumpChunk::canAlloc(size_t n)
{
    char *aligned = AlignPtr(bump);
    char *bumped = aligned + n;
    return bumped <= limit && bumped > headerBase();
}

} // namespace detail
} // namespace js

void
LifoAlloc::freeAll()
{
    while (first) {
        BumpChunk *victim = first;
        first = first->next();
        decrementCurSize(victim->computedSizeOfIncludingThis());
        BumpChunk::delete_(victim);
    }
    first = latest = last = NULL;

    // Nb: maintaining curSize_ correctly isn't easy.  Fortunately, this is an
    // excellent sanity check.
    JS_ASSERT(curSize_ == 0);
}

LifoAlloc::BumpChunk *
LifoAlloc::getOrCreateChunk(size_t n)
{
    if (first) {
        // Look for existing, unused BumpChunks to satisfy the request.
        while (latest->next()) {
            latest = latest->next();
            latest->resetBump();    // This was an unused BumpChunk on the chain.
            if (latest->canAlloc(n))
                return latest;
        }
    }

    size_t defaultChunkFreeSpace = defaultChunkSize_ - sizeof(BumpChunk);
    size_t chunkSize;
    if (n > defaultChunkFreeSpace) {
        size_t allocSizeWithHeader = n + sizeof(BumpChunk);

        // Guard for overflow.
        if (allocSizeWithHeader < n ||
            (allocSizeWithHeader & (size_t(1) << (tl::BitSize<size_t>::result - 1)))) {
            return NULL;
        }

        chunkSize = RoundUpPow2(allocSizeWithHeader);
    } else {
        chunkSize = defaultChunkSize_;
    }

    // If we get here, we couldn't find an existing BumpChunk to fill the request.
    BumpChunk *newChunk = BumpChunk::new_(chunkSize);
    if (!newChunk)
        return NULL;
    if (!first) {
        latest = first = last = newChunk;
    } else {
        JS_ASSERT(latest && !latest->next());
        latest->setNext(newChunk);
        latest = last = newChunk;
    }

    size_t computedChunkSize = newChunk->computedSizeOfIncludingThis();
    JS_ASSERT(computedChunkSize == chunkSize);
    incrementCurSize(computedChunkSize);

    return newChunk;
}

void
LifoAlloc::transferFrom(LifoAlloc *other)
{
    JS_ASSERT(!markCount);
    JS_ASSERT(latest == first);
    JS_ASSERT(!other->markCount);

    if (!other->first)
        return;

    incrementCurSize(other->curSize_);
    append(other->first, other->last);
    other->first = other->last = other->latest = NULL;
    other->curSize_ = 0;
}

void
LifoAlloc::transferUnusedFrom(LifoAlloc *other)
{
    JS_ASSERT(!markCount);
    JS_ASSERT(latest == first);

    if (other->markCount || !other->first)
        return;

    // Transfer all chunks *after* |latest|.

    if (other->latest->next()) {
        if (other->latest == other->first) {
            // We're transferring everything except the first chunk.
            size_t delta = other->curSize_ - other->first->computedSizeOfIncludingThis();
            other->decrementCurSize(delta);
            incrementCurSize(delta);
        } else {
            for (BumpChunk *chunk = other->latest->next(); chunk; chunk = chunk->next()) {
                size_t size = chunk->computedSizeOfIncludingThis();
                incrementCurSize(size);
                other->decrementCurSize(size);
            }
        }

        append(other->latest->next(), other->last);
        other->latest->setNext(NULL);
        other->last = other->latest;
    }
}
