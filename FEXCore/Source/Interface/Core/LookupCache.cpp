// SPDX-License-Identifier: MIT
/*
$info$
tags: glue|block-database
desc: Stores information about blocks, and provides C++ implementations to lookup the blocks
$end_info$
*/

#include <FEXCore/Utils/Allocator.h>
#include <FEXCore/Utils/LogManager.h>
#include <FEXCore/HLE/SyscallHandler.h>

#include "Interface/Context/Context.h"
#include "Interface/Core/LookupCache.h"

namespace FEXCore {
GuestToHostMap::GuestToHostMap()
  : BlockLinks_mbr {"FEXMem_BlockLinks"} {
  BlockLinks_pma = fextl::make_unique<std::pmr::polymorphic_allocator<std::byte>>(&BlockLinks_mbr);
  // Setup our PMR map.
  BlockLinks = BlockLinks_pma->new_object<BlockLinksMapType>();
}

LookupCache::LookupCache(FEXCore::Context::ContextImpl* CTX)
  : ctx {CTX} {

  if (DisableL2Cache()) {
    TotalCacheSize = MAX_L1_SIZE;
    PagePointer = reinterpret_cast<uintptr_t>(FEXCore::Allocator::VirtualAlloc(TotalCacheSize, false, false));
    if (PagePointer == 0 || PagePointer == static_cast<uintptr_t>(-1ULL)) {
      TotalCacheSize = MIN_L1_ENTRIES * sizeof(LookupCacheEntry);
      PagePointer = reinterpret_cast<uintptr_t>(FEXCore::Allocator::VirtualAlloc(TotalCacheSize, false, false));
    }
    if (PagePointer == 0 || PagePointer == static_cast<uintptr_t>(-1ULL)) {
      LogMan::Msg::EFmt("LookupCache: Failed to allocate PagePointer for L1 cache (TotalCacheSize={})", TotalCacheSize);
      std::abort();
    }
    PageMemory = 0;
    L1Pointer = PagePointer;
  } else {
    TotalCacheSize = ctx->Config.VirtualMemSize / FEXCore::Utils::FEX_PAGE_SIZE * 8 + CODE_SIZE + MAX_L1_SIZE;
    PagePointer = reinterpret_cast<uintptr_t>(FEXCore::Allocator::VirtualAlloc(TotalCacheSize, false, false));
    if (PagePointer == 0 || PagePointer == static_cast<uintptr_t>(-1ULL)) {
      // Fallback to L1-only cache to preserve address space
      TotalCacheSize = MAX_L1_SIZE;
      PagePointer = reinterpret_cast<uintptr_t>(FEXCore::Allocator::VirtualAlloc(TotalCacheSize, false, false));
      if (PagePointer == 0 || PagePointer == static_cast<uintptr_t>(-1ULL)) {
        TotalCacheSize = MIN_L1_ENTRIES * sizeof(LookupCacheEntry);
        PagePointer = reinterpret_cast<uintptr_t>(FEXCore::Allocator::VirtualAlloc(TotalCacheSize, false, false));
      }
      if (PagePointer == 0 || PagePointer == static_cast<uintptr_t>(-1ULL)) {
        LogMan::Msg::EFmt("LookupCache: Failed to allocate PagePointer fallback (TotalCacheSize={})", TotalCacheSize);
        std::abort();
      }
      PageMemory = 0;
      L1Pointer = PagePointer;
    } else {
      PageMemory = PagePointer + ctx->Config.VirtualMemSize / FEXCore::Utils::FEX_PAGE_SIZE * 8;
      L1Pointer = PageMemory + CODE_SIZE;
    }
  }

  // Disable THP on the Lookup cache.
  FEXCore::Allocator::VirtualTHPControl(reinterpret_cast<const void*>(PagePointer), TotalCacheSize, FEXCore::Allocator::THPControl::Disable);

  if (PageMemory != 0) {
    FEXCore::Allocator::VirtualName("FEXMem_Lookup", reinterpret_cast<void*>(PagePointer),
                                    ctx->Config.VirtualMemSize / FEXCore::Utils::FEX_PAGE_SIZE * 8 + CODE_SIZE);
  }
  if (CTX && CTX->SyscallHandler) {
    CTX->SyscallHandler->MarkOvercommitRange(PagePointer, TotalCacheSize);
  }

  FEXCore::Allocator::VirtualName("FEXMem_Lookup_L1", reinterpret_cast<void*>(L1Pointer), MAX_L1_SIZE);

  VirtualMemSize = ctx->Config.VirtualMemSize;

  if (DynamicL1Cache()) {
    // Start at minimum size when dynamic.
    L1PointerMask = MIN_L1_ENTRIES - 1;
  } else {
    // Start at maximum instead.
    L1PointerMask = MAX_L1_ENTRIES - 1;
  }
}

LookupCache::~LookupCache() {
  if (PagePointer != 0 && PagePointer != static_cast<uintptr_t>(-1ULL)) {
    FEXCore::Allocator::VirtualFree(reinterpret_cast<void*>(PagePointer), TotalCacheSize);
    if (ctx && ctx->SyscallHandler) {
      ctx->SyscallHandler->UnmarkOvercommitRange(PagePointer, TotalCacheSize);
    }
  }

  // No need to free BlockLinks map.
  // These will get freed when their memory allocators are deallocated.
}

void LookupCache::ClearL2Cache(const FEXCore::LookupCacheBaseLockToken& lk) {
  // Clear out the page memory
  // PagePointer and PageMemory are sequential with each other. Clear both at once.
  if (!DisableL2Cache() && PageMemory != 0 && PagePointer != 0) {
    FEXCore::Allocator::VirtualDontNeed(reinterpret_cast<void*>(PagePointer),
                                        ctx->Config.VirtualMemSize / FEXCore::Utils::FEX_PAGE_SIZE * 8 + CODE_SIZE, false);
    AllocateOffset = 0;
  }
}

void LookupCache::ClearThreadLocalCaches(const LookupCacheWriteLockToken&) {
  // Clear L1 and L2 by clearing the full cache.
  if (PagePointer != 0 && PagePointer != static_cast<uintptr_t>(-1ULL)) {
    FEXCore::Allocator::VirtualDontNeed(reinterpret_cast<void*>(PagePointer), TotalCacheSize, false);
  }

  CachedCodePages.clear();
}

void LookupCache::ClearCache(const LookupCacheWriteLockToken& lk) {
  // Clear L1 and L2 by clearing the full cache.
  ClearThreadLocalCaches(lk);
  Shared->ClearCache(lk);
}

void GuestToHostMap::ClearCache(const LookupCacheWriteLockToken&) {
  // Sever every recorded direct-branch link before discarding them. Erase(Address, ...) already
  // does this correctly for a single address (walk BlockLinks, call each delinker with its
  // HostLink), but until now this full-cache clear just replaced BlockLinks with a fresh empty
  // map, silently dropping every entry without ever calling its delinker. On every platform that
  // only ever grows into fresh memory on eviction (NewCodeBuffer=true, the normal case
  // everywhere) that gap was invisible: the stale patched branches point at addresses inside the
  // now-abandoned old buffer, which nothing valid ever branches back into again anyway. It
  // stopped being invisible on iOS, where JIT buffer reuse-in-place (NewCodeBuffer=false) was
  // tried as a workaround for a separate StikDebug allocation-count limitation: reusing the same
  // memory for new compiled code, with old delinkers never having been called, left other
  // still-referenced code's already-patched direct branches pointing at addresses that now held
  // completely unrelated new code. HandleGuestSignal's SIGBUS-recovery paths could keep
  // "fixing" the resulting faults' host/writable-vs-executable address one instruction at a
  // time, but never the actual stale target underneath -- confirmed on-device as an unbounded
  // fault loop (7,000,000+ recoveries cycling through only 8 distinct addresses, no crash, no
  // progress). Actually severing every link here, the same way Erase() already does for one
  // address, is the real fix for that, independent of whichever eviction mode ends up used.
  for (auto& [Tag, Delinker] : *BlockLinks) {
    Delinker(Tag.HostLink);
  }

  // Allocate a new pointer from the BlockLinks pma again.
  BlockLinks = BlockLinks_pma->new_object<BlockLinksMapType>();
  // All code is gone, clear the block list
  BlockList.clear();
}

} // namespace FEXCore
