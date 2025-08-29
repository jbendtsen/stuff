package main

import (
    "sync"
	"sync/atomic"
)

type Link[T any] struct {
	value T
	next *Link[T]
}

type LocklessLinkedStack[T any] struct {
	stack atomic.Pointer[Link[T]]
}

type Blocker struct {
    inUse bool
    cv *sync.Cond
    mtx sync.Locker
}

type MapLock struct {
    blockerStack *LocklessLinkedStack[*Blocker]
    lockMap *sync.Map
}

func (lls *LocklessLinkedStack[T]) Pull() (T, bool) {
	for true {
		cur := lls.stack.Load()
		if cur == nil {
			break
		}
		next := cur.next
		if lls.stack.CompareAndSwap(cur, next) {
			return cur.value, true
		}
	}
	var defaultValue T
	return defaultValue, false
}

func (lls *LocklessLinkedStack[T]) Push(value T) {
	newLink := &Link[T]{}
	newLink.value = value
	for true {
		cur := lls.stack.Load()
		newLink.next = cur
		if lls.stack.CompareAndSwap(cur, newLink) {
			break
		}
	}
}

func MakeBlocker() *Blocker {
    mtx := &sync.Mutex{}
    return &Blocker{
        inUse: false,
        mtx: mtx,
        cv: sync.NewCond(mtx),
    }
}

func (m *MapLock) Acquire(key string) {
    newBlocker, found := m.blockerStack.Pull()
    if !found {
        newBlocker = MakeBlocker()
    }
    blockerObj, exists := m.lockMap.LoadOrStore(key, newBlocker)
    blocker, ok := blockerObj.(*Blocker)
    if !ok {
        return
    }
    // If there wasn't any Blocker for this key, then blocker == newBlocker.
    if exists {
        // Otherwise, we have no use for this blocker, so we can put it back for the next call to Acquire().
        m.blockerStack.Push(newBlocker)
    }
    blocker.mtx.Lock()
    for blocker.inUse {
        blocker.cv.Wait()
    }
    blocker.inUse = true
}

func (m *MapLock) Release(key string) {
    blockerObj, _ := m.lockMap.Load(key)
    blocker, _ := blockerObj.(*Blocker)
    if blocker == nil {
        return
    }
    blocker.inUse = false
    blocker.cv.Broadcast()
    blocker.mtx.Unlock()
}

func MakeMapLock() MapLock {
    return MapLock {
        blockerStack: &LocklessLinkedStack[*Blocker]{},
        lockMap: &sync.Map{},
    }
}
