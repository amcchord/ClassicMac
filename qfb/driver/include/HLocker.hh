#ifndef HLOCKERHH
#define HLOCKERHH

#include <Multiverse.h>

#ifdef SUPPORT_AUX

/* If we're supporting A/UX, we can't use MoveHHi or HUnlock. Which is weird.
   So we just lock the handle in our Open routine, and we get slightly more
   efficient access to our locals in exchange for being bad Mac citizens. */
template<class T> class HLocker {
  T* inner;
public:
  HLocker(Handle locked) : inner(*reinterpret_cast<T**>(locked)) {}
  void unlock() {}
  T* operator*() const { return (T*)StripAddress((Ptr)inner); }
  T* operator->() const { return (T*)StripAddress((Ptr)inner); }
};

#else

/* If we don't have to support A/UX, we can use MoveHHi and lock/unlock our
   handle to be good Mac citizens. */
template<class T> class HLocker {
  T** locked;
public:
  HLocker(Handle locked) : locked(reinterpret_cast<T**>(locked)) {
    HLock(locked);
  }
  ~HLocker() {
    unlock();
  }
  void unlock() {
    if(locked != nullptr) {
      HUnlock(reinterpret_cast<Handle>(locked));
      locked = nullptr;
    }
  }
  T* operator*() const { return *locked; }
  T* operator->() const { return *locked; }
};

#endif

#endif
