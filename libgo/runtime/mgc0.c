// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Garbage collector.

#include <unistd.h>

#include "runtime.h"
#include "arch.h"
#include "malloc.h"
#include "mgc0.h"
#include "race.h"
#include "go-type.h"

// Map gccgo field names to gc field names.
// Slice aka __go_open_array.
#define array __values
#define cap __capacity
// Iface aka __go_interface
#define tab __methods
// Eface aka __go_empty_interface.
#define type __type_descriptor
// Hmap aka __go_map
typedef struct __go_map Hmap;
// Type aka __go_type_descriptor
#define kind __code
#define string __reflection
#define KindPtr GO_PTR
#define KindNoPointers GO_NO_POINTERS
// PtrType aka __go_ptr_type
#define elem __element_type

#ifdef USING_SPLIT_STACK

extern void * __splitstack_find (void *, void *, size_t *, void **, void **,
				 void **);

extern void * __splitstack_find_context (void *context[10], size_t *, void **,
					 void **, void **);

#endif

enum {
	Debug = 0,
	DebugMark = 0,  // run second pass to check mark
	CollectStats = 0,
	ScanStackByFrames = 0,
	IgnorePreciseGC = 0,

	// Four bits per word (see #defines below).
	wordsPerBitmapWord = sizeof(void*)*8/4,
	bitShift = sizeof(void*)*8/4,

	handoffThreshold = 4,
	IntermediateBufferCapacity = 64,

	// Bits in type information
	PRECISE = 1,
	LOOP = 2,
	PC_BITS = PRECISE | LOOP,
};

// Bits in per-word bitmap.
// #defines because enum might not be able to hold the values.
//
// Each word in the bitmap describes wordsPerBitmapWord words
// of heap memory.  There are 4 bitmap bits dedicated to each heap word,
// so on a 64-bit system there is one bitmap word per 16 heap words.
// The bits in the word are packed together by type first, then by
// heap location, so each 64-bit bitmap word consists of, from top to bottom,
// the 16 bitSpecial bits for the corresponding heap words, then the 16 bitMarked bits,
// then the 16 bitNoPointers/bitBlockBoundary bits, then the 16 bitAllocated bits.
// This layout makes it easier to iterate over the bits of a given type.
//
// The bitmap starts at mheap.arena_start and extends *backward* from
// there.  On a 64-bit system the off'th word in the arena is tracked by
// the off/16+1'th word before mheap.arena_start.  (On a 32-bit system,
// the only difference is that the divisor is 8.)
//
// To pull out the bits corresponding to a given pointer p, we use:
//
//	off = p - (uintptr*)mheap.arena_start;  // word offset
//	b = (uintptr*)mheap.arena_start - off/wordsPerBitmapWord - 1;
//	shift = off % wordsPerBitmapWord
//	bits = *b >> shift;
//	/* then test bits & bitAllocated, bits & bitMarked, etc. */
//
#define bitAllocated		((uintptr)1<<(bitShift*0))
#define bitNoPointers		((uintptr)1<<(bitShift*1))	/* when bitAllocated is set */
#define bitMarked		((uintptr)1<<(bitShift*2))	/* when bitAllocated is set */
#define bitSpecial		((uintptr)1<<(bitShift*3))	/* when bitAllocated is set - has finalizer or being profiled */
#define bitBlockBoundary	((uintptr)1<<(bitShift*1))	/* when bitAllocated is NOT set */

#define bitMask (bitBlockBoundary | bitAllocated | bitMarked | bitSpecial)

// Holding worldsema grants an M the right to try to stop the world.
// The procedure is:
//
//	runtime_semacquire(&runtime_worldsema);
//	m->gcing = 1;
//	runtime_stoptheworld();
//
//	... do stuff ...
//
//	m->gcing = 0;
//	runtime_semrelease(&runtime_worldsema);
//	runtime_starttheworld();
//
uint32 runtime_worldsema = 1;

static int32 gctrace;

// The size of Workbuf is N*PageSize.
typedef struct Workbuf Workbuf;
struct Workbuf
{
#define SIZE (2*PageSize-sizeof(LFNode)-sizeof(uintptr))
	LFNode  node; // must be first
	uintptr nobj;
	Obj     obj[SIZE/sizeof(Obj) - 1];
	uint8   _padding[SIZE%sizeof(Obj) + sizeof(Obj)];
#undef SIZE
};

typedef struct Finalizer Finalizer;
struct Finalizer
{
	FuncVal *fn;
	void *arg;
	const struct __go_func_type *ft;
};

typedef struct FinBlock FinBlock;
struct FinBlock
{
	FinBlock *alllink;
	FinBlock *next;
	int32 cnt;
	int32 cap;
	Finalizer fin[1];
};

static G *fing;
static FinBlock *finq; // list of finalizers that are to be executed
static FinBlock *finc; // cache of free blocks
static FinBlock *allfin; // list of all blocks
static Lock finlock;
static int32 fingwait;

static void runfinq(void*);
static Workbuf* getempty(Workbuf*);
static Workbuf* getfull(Workbuf*);
static void	putempty(Workbuf*);
static Workbuf* handoff(Workbuf*);
static void	gchelperstart(void);

static struct {
	uint64	full;  // lock-free list of full blocks
	uint64	empty; // lock-free list of empty blocks
	byte	pad0[CacheLineSize]; // prevents false-sharing between full/empty and nproc/nwait
	uint32	nproc;
	volatile uint32	nwait;
	volatile uint32	ndone;
	volatile uint32 debugmarkdone;
	Note	alldone;
	ParFor	*markfor;
	ParFor	*sweepfor;

	Lock;
	byte	*chunk;
	uintptr	nchunk;

	Obj	*roots;
	uint32	nroot;
	uint32	rootcap;
} work;

enum {
	GC_DEFAULT_PTR = GC_NUM_INSTR,
	GC_MAP_NEXT,
	GC_CHAN,

	GC_NUM_INSTR2
};

static struct {
	struct {
		uint64 sum;
		uint64 cnt;
	} ptr;
	uint64 nbytes;
	struct {
		uint64 sum;
		uint64 cnt;
		uint64 notype;
		uint64 typelookup;
	} obj;
	uint64 rescan;
	uint64 rescanbytes;
	uint64 instr[GC_NUM_INSTR2];
	uint64 putempty;
	uint64 getfull;
} gcstats;

// markonly marks an object. It returns true if the object
// has been marked by this function, false otherwise.
// This function doesn't append the object to any buffer.
static bool
markonly(void *obj)
{
	byte *p;
	uintptr *bitp, bits, shift, x, xbits, off;
	MSpan *s;
	PageID k;

	// Words outside the arena cannot be pointers.
	if((byte*)obj < runtime_mheap->arena_start || (byte*)obj >= runtime_mheap->arena_used)
		return false;

	// obj may be a pointer to a live object.
	// Try to find the beginning of the object.

	// Round down to word boundary.
	obj = (void*)((uintptr)obj & ~((uintptr)PtrSize-1));

	// Find bits for this word.
	off = (uintptr*)obj - (uintptr*)runtime_mheap->arena_start;
	bitp = (uintptr*)runtime_mheap->arena_start - off/wordsPerBitmapWord - 1;
	shift = off % wordsPerBitmapWord;
	xbits = *bitp;
	bits = xbits >> shift;

	// Pointing at the beginning of a block?
	if((bits & (bitAllocated|bitBlockBoundary)) != 0)
		goto found;

	// Otherwise consult span table to find beginning.
	// (Manually inlined copy of MHeap_LookupMaybe.)
	k = (uintptr)obj>>PageShift;
	x = k;
	if(sizeof(void*) == 8)
		x -= (uintptr)runtime_mheap->arena_start>>PageShift;
	s = runtime_mheap->map[x];
	if(s == nil || k < s->start || k - s->start >= s->npages || s->state != MSpanInUse)
		return false;
	p = (byte*)((uintptr)s->start<<PageShift);
	if(s->sizeclass == 0) {
		obj = p;
	} else {
		if((byte*)obj >= (byte*)s->limit)
			return false;
		uintptr size = s->elemsize;
		int32 i = ((byte*)obj - p)/size;
		obj = p+i*size;
	}

	// Now that we know the object header, reload bits.
	off = (uintptr*)obj - (uintptr*)runtime_mheap->arena_start;
	bitp = (uintptr*)runtime_mheap->arena_start - off/wordsPerBitmapWord - 1;
	shift = off % wordsPerBitmapWord;
	xbits = *bitp;
	bits = xbits >> shift;

found:
	// Now we have bits, bitp, and shift correct for
	// obj pointing at the base of the object.
	// Only care about allocated and not marked.
	if((bits & (bitAllocated|bitMarked)) != bitAllocated)
		return false;
	if(work.nproc == 1)
		*bitp |= bitMarked<<shift;
	else {
		for(;;) {
			x = *bitp;
			if(x & (bitMarked<<shift))
				return false;
			if(runtime_casp((void**)bitp, (void*)x, (void*)(x|(bitMarked<<shift))))
				break;
		}
	}

	// The object is now marked
	return true;
}

// PtrTarget is a structure used by intermediate buffers.
// The intermediate buffers hold GC data before it
// is moved/flushed to the work buffer (Workbuf).
// The size of an intermediate buffer is very small,
// such as 32 or 64 elements.
typedef struct PtrTarget PtrTarget;
struct PtrTarget
{
	void *p;
	uintptr ti;
};

typedef struct BufferList BufferList;
struct BufferList
{
	PtrTarget ptrtarget[IntermediateBufferCapacity];
	Obj obj[IntermediateBufferCapacity];
	uint32 busy;
	byte pad[CacheLineSize];
};
static BufferList bufferList[MaxGcproc];

static Type *itabtype;

static void enqueue(Obj obj, Workbuf **_wbuf, Obj **_wp, uintptr *_nobj);

// flushptrbuf moves data from the PtrTarget buffer to the work buffer.
// The PtrTarget buffer contains blocks irrespective of whether the blocks have been marked or scanned,
// while the work buffer contains blocks which have been marked
// and are prepared to be scanned by the garbage collector.
//
// _wp, _wbuf, _nobj are input/output parameters and are specifying the work buffer.
//
// A simplified drawing explaining how the todo-list moves from a structure to another:
//
//     scanblock
//  (find pointers)
//    Obj ------> PtrTarget (pointer targets)
//     ↑          |
//     |          |
//     `----------'
//     flushptrbuf
//  (find block start, mark and enqueue)
static void
flushptrbuf(PtrTarget *ptrbuf, PtrTarget **ptrbufpos, Obj **_wp, Workbuf **_wbuf, uintptr *_nobj)
{
	byte *p, *arena_start, *obj;
	uintptr size, *bitp, bits, shift, j, x, xbits, off, nobj, ti, n;
	MSpan *s;
	PageID k;
	Obj *wp;
	Workbuf *wbuf;
	PtrTarget *ptrbuf_end;

	arena_start = runtime_mheap->arena_start;

	wp = *_wp;
	wbuf = *_wbuf;
	nobj = *_nobj;

	ptrbuf_end = *ptrbufpos;
	n = ptrbuf_end - ptrbuf;
	*ptrbufpos = ptrbuf;

	if(CollectStats) {
		runtime_xadd64(&gcstats.ptr.sum, n);
		runtime_xadd64(&gcstats.ptr.cnt, 1);
	}

	// If buffer is nearly full, get a new one.
	if(wbuf == nil || nobj+n >= nelem(wbuf->obj)) {
		if(wbuf != nil)
			wbuf->nobj = nobj;
		wbuf = getempty(wbuf);
		wp = wbuf->obj;
		nobj = 0;

		if(n >= nelem(wbuf->obj))
			runtime_throw("ptrbuf has to be smaller than WorkBuf");
	}

	// TODO(atom): This block is a branch of an if-then-else statement.
	//             The single-threaded branch may be added in a next CL.
	{
		// Multi-threaded version.

		while(ptrbuf < ptrbuf_end) {
			obj = ptrbuf->p;
			ti = ptrbuf->ti;
			ptrbuf++;

			// obj belongs to interval [mheap.arena_start, mheap.arena_used).
			if(Debug > 1) {
				if(obj < runtime_mheap->arena_start || obj >= runtime_mheap->arena_used)
					runtime_throw("object is outside of mheap");
			}

			// obj may be a pointer to a live object.
			// Try to find the beginning of the object.

			// Round down to word boundary.
			if(((uintptr)obj & ((uintptr)PtrSize-1)) != 0) {
				obj = (void*)((uintptr)obj & ~((uintptr)PtrSize-1));
				ti = 0;
			}

			// Find bits for this word.
			off = (uintptr*)obj - (uintptr*)arena_start;
			bitp = (uintptr*)arena_start - off/wordsPerBitmapWord - 1;
			shift = off % wordsPerBitmapWord;
			xbits = *bitp;
			bits = xbits >> shift;

			// Pointing at the beginning of a block?
			if((bits & (bitAllocated|bitBlockBoundary)) != 0)
				goto found;

			ti = 0;

			// Pointing just past the beginning?
			// Scan backward a little to find a block boundary.
			for(j=shift; j-->0; ) {
				if(((xbits>>j) & (bitAllocated|bitBlockBoundary)) != 0) {
					obj = (byte*)obj - (shift-j)*PtrSize;
					shift = j;
					bits = xbits>>shift;
					goto found;
				}
			}

			// Otherwise consult span table to find beginning.
			// (Manually inlined copy of MHeap_LookupMaybe.)
			k = (uintptr)obj>>PageShift;
			x = k;
			if(sizeof(void*) == 8)
				x -= (uintptr)arena_start>>PageShift;
			s = runtime_mheap->map[x];
			if(s == nil || k < s->start || k - s->start >= s->npages || s->state != MSpanInUse)
				continue;
			p = (byte*)((uintptr)s->start<<PageShift);
			if(s->sizeclass == 0) {
				obj = p;
			} else {
				if((byte*)obj >= (byte*)s->limit)
					continue;
				size = s->elemsize;
				int32 i = ((byte*)obj - p)/size;
				obj = p+i*size;
			}

			// Now that we know the object header, reload bits.
			off = (uintptr*)obj - (uintptr*)arena_start;
			bitp = (uintptr*)arena_start - off/wordsPerBitmapWord - 1;
			shift = off % wordsPerBitmapWord;
			xbits = *bitp;
			bits = xbits >> shift;

		found:
			// Now we have bits, bitp, and shift correct for
			// obj pointing at the base of the object.
			// Only care about allocated and not marked.
			if((bits & (bitAllocated|bitMarked)) != bitAllocated)
				continue;
			if(work.nproc == 1)
				*bitp |= bitMarked<<shift;
			else {
				for(;;) {
					x = *bitp;
					if(x & (bitMarked<<shift))
						goto continue_obj;
					if(runtime_casp((void**)bitp, (void*)x, (void*)(x|(bitMarked<<shift))))
						break;
				}
			}

			// If object has no pointers, don't need to scan further.
			if((bits & bitNoPointers) != 0)
				continue;

			// Ask span about size class.
			// (Manually inlined copy of MHeap_Lookup.)
			x = (uintptr)obj >> PageShift;
			if(sizeof(void*) == 8)
				x -= (uintptr)arena_start>>PageShift;
			s = runtime_mheap->map[x];

			PREFETCH(obj);

			*wp = (Obj){obj, s->elemsize, ti};
			wp++;
			nobj++;
		continue_obj:;
		}

		// If another proc wants a pointer, give it some.
		if(work.nwait > 0 && nobj > handoffThreshold && work.full == 0) {
			wbuf->nobj = nobj;
			wbuf = handoff(wbuf);
			nobj = wbuf->nobj;
			wp = wbuf->obj + nobj;
		}
	}

	*_wp = wp;
	*_wbuf = wbuf;
	*_nobj = nobj;
}

static void
flushobjbuf(Obj *objbuf, Obj **objbufpos, Obj **_wp, Workbuf **_wbuf, uintptr *_nobj)
{
	uintptr nobj, off;
	Obj *wp, obj;
	Workbuf *wbuf;
	Obj *objbuf_end;

	wp = *_wp;
	wbuf = *_wbuf;
	nobj = *_nobj;

	objbuf_end = *objbufpos;
	*objbufpos = objbuf;

	while(objbuf < objbuf_end) {
		obj = *objbuf++;

		// Align obj.b to a word boundary.
		off = (uintptr)obj.p & (PtrSize-1);
		if(off != 0) {
			obj.p += PtrSize - off;
			obj.n -= PtrSize - off;
			obj.ti = 0;
		}

		if(obj.p == nil || obj.n == 0)
			continue;

		// If buffer is full, get a new one.
		if(wbuf == nil || nobj >= nelem(wbuf->obj)) {
			if(wbuf != nil)
				wbuf->nobj = nobj;
			wbuf = getempty(wbuf);
			wp = wbuf->obj;
			nobj = 0;
		}

		*wp = obj;
		wp++;
		nobj++;
	}

	// If another proc wants a pointer, give it some.
	if(work.nwait > 0 && nobj > handoffThreshold && work.full == 0) {
		wbuf->nobj = nobj;
		wbuf = handoff(wbuf);
		nobj = wbuf->nobj;
		wp = wbuf->obj + nobj;
	}

	*_wp = wp;
	*_wbuf = wbuf;
	*_nobj = nobj;
}

// Program that scans the whole block and treats every block element as a potential pointer
static uintptr defaultProg[2] = {PtrSize, GC_DEFAULT_PTR};

#if 0
// Hashmap iterator program
static uintptr mapProg[2] = {0, GC_MAP_NEXT};

// Hchan program
static uintptr chanProg[2] = {0, GC_CHAN};
#endif

// Local variables of a program fragment or loop
typedef struct Frame Frame;
struct Frame {
	uintptr count, elemsize, b;
	uintptr *loop_or_ret;
};

// Sanity check for the derived type info objti.
static void
checkptr(void *obj, uintptr objti)
{
	uintptr type, tisize, i, x;
	byte *objstart;
	Type *t;
	MSpan *s;

	if(!Debug)
		runtime_throw("checkptr is debug only");

	if((byte*)obj < runtime_mheap->arena_start || (byte*)obj >= runtime_mheap->arena_used)
		return;
	type = runtime_gettype(obj);
	t = (Type*)(type & ~(uintptr)(PtrSize-1));
	if(t == nil)
		return;
	x = (uintptr)obj >> PageShift;
	if(sizeof(void*) == 8)
		x -= (uintptr)(runtime_mheap->arena_start)>>PageShift;
	s = runtime_mheap->map[x];
	objstart = (byte*)((uintptr)s->start<<PageShift);
	if(s->sizeclass != 0) {
		i = ((byte*)obj - objstart)/s->elemsize;
		objstart += i*s->elemsize;
	}
	tisize = *(uintptr*)objti;
	// Sanity check for object size: it should fit into the memory block.
	if((byte*)obj + tisize > objstart + s->elemsize)
		runtime_throw("invalid gc type info");
	if(obj != objstart)
		return;
	// If obj points to the beginning of the memory block,
	// check type info as well.
	if(t->string == nil ||
		// Gob allocates unsafe pointers for indirection.
		(runtime_strcmp((const char *)t->string->str, (const char*)"unsafe.Pointer") &&
		// Runtime and gc think differently about closures.
		 runtime_strstr((const char *)t->string->str, (const char*)"struct { F uintptr") != (const char *)t->string->str)) {
#if 0
		pc1 = (uintptr*)objti;
		pc2 = (uintptr*)t->gc;
		// A simple best-effort check until first GC_END.
		for(j = 1; pc1[j] != GC_END && pc2[j] != GC_END; j++) {
			if(pc1[j] != pc2[j]) {
				runtime_printf("invalid gc type info for '%s' at %p, type info %p, block info %p\n",
					t->string ? (const int8*)t->string->str : (const int8*)"?", j, pc1[j], pc2[j]);
				runtime_throw("invalid gc type info");
			}
		}
#endif
	}
}					

// scanblock scans a block of n bytes starting at pointer b for references
// to other objects, scanning any it finds recursively until there are no
// unscanned objects left.  Instead of using an explicit recursion, it keeps
// a work list in the Workbuf* structures and loops in the main function
// body.  Keeping an explicit work list is easier on the stack allocator and
// more efficient.
//
// wbuf: current work buffer
// wp:   storage for next queued pointer (write pointer)
// nobj: number of queued objects
static void
scanblock(Workbuf *wbuf, Obj *wp, uintptr nobj, bool keepworking)
{
	byte *b, *arena_start, *arena_used;
	uintptr n, i, end_b, elemsize, size, ti, objti, count /* , type */;
	uintptr *pc, precise_type, nominal_size;
#if 0
	uintptr *map_ret, mapkey_size, mapval_size, mapkey_ti, mapval_ti, *chan_ret, chancap;
#endif
	void *obj;
	const Type *t;
	Slice *sliceptr;
	Frame *stack_ptr, stack_top, stack[GC_STACK_CAPACITY+4];
	BufferList *scanbuffers;
	PtrTarget *ptrbuf, *ptrbuf_end, *ptrbufpos;
	Obj *objbuf, *objbuf_end, *objbufpos;
	Eface *eface;
	Iface *iface;
#if 0
	Hmap *hmap;
	MapType *maptype;
	bool mapkey_kind, mapval_kind;
	struct hash_gciter map_iter;
	struct hash_gciter_data d;
	Hchan *chan;
	ChanType *chantype;
#endif

	if(sizeof(Workbuf) % PageSize != 0)
		runtime_throw("scanblock: size of Workbuf is suboptimal");

	// Memory arena parameters.
	arena_start = runtime_mheap->arena_start;
	arena_used = runtime_mheap->arena_used;

	stack_ptr = stack+nelem(stack)-1;
	
	precise_type = false;
	nominal_size = 0;

	// Allocate ptrbuf
	{
		scanbuffers = &bufferList[runtime_m()->helpgc];
		ptrbuf = &scanbuffers->ptrtarget[0];
		ptrbuf_end = &scanbuffers->ptrtarget[0] + nelem(scanbuffers->ptrtarget);
		objbuf = &scanbuffers->obj[0];
		objbuf_end = &scanbuffers->obj[0] + nelem(scanbuffers->obj);
	}

	ptrbufpos = ptrbuf;
	objbufpos = objbuf;

	// (Silence the compiler)
#if 0
	map_ret = nil;
	mapkey_size = mapval_size = 0;
	mapkey_kind = mapval_kind = false;
	mapkey_ti = mapval_ti = 0;
	chan = nil;
	chantype = nil;
	chan_ret = nil;
#endif

	goto next_block;

	for(;;) {
		// Each iteration scans the block b of length n, queueing pointers in
		// the work buffer.
		if(Debug > 1) {
			runtime_printf("scanblock %p %D\n", b, (int64)n);
		}

		if(CollectStats) {
			runtime_xadd64(&gcstats.nbytes, n);
			runtime_xadd64(&gcstats.obj.sum, nobj);
			runtime_xadd64(&gcstats.obj.cnt, 1);
		}

		if(ti != 0 && false) {
			pc = (uintptr*)(ti & ~(uintptr)PC_BITS);
			precise_type = (ti & PRECISE);
			stack_top.elemsize = pc[0];
			if(!precise_type)
				nominal_size = pc[0];
			if(ti & LOOP) {
				stack_top.count = 0;	// 0 means an infinite number of iterations
				stack_top.loop_or_ret = pc+1;
			} else {
				stack_top.count = 1;
			}
			if(Debug) {
				// Simple sanity check for provided type info ti:
				// The declared size of the object must be not larger than the actual size
				// (it can be smaller due to inferior pointers).
				// It's difficult to make a comprehensive check due to inferior pointers,
				// reflection, gob, etc.
				if(pc[0] > n) {
					runtime_printf("invalid gc type info: type info size %p, block size %p\n", pc[0], n);
					runtime_throw("invalid gc type info");
				}
			}
		} else if(UseSpanType && false) {
			if(CollectStats)
				runtime_xadd64(&gcstats.obj.notype, 1);

#if 0
			type = runtime_gettype(b);
			if(type != 0) {
				if(CollectStats)
					runtime_xadd64(&gcstats.obj.typelookup, 1);

				t = (Type*)(type & ~(uintptr)(PtrSize-1));
				switch(type & (PtrSize-1)) {
				case TypeInfo_SingleObject:
					pc = (uintptr*)t->gc;
					precise_type = true;  // type information about 'b' is precise
					stack_top.count = 1;
					stack_top.elemsize = pc[0];
					break;
				case TypeInfo_Array:
					pc = (uintptr*)t->gc;
					if(pc[0] == 0)
						goto next_block;
					precise_type = true;  // type information about 'b' is precise
					stack_top.count = 0;  // 0 means an infinite number of iterations
					stack_top.elemsize = pc[0];
					stack_top.loop_or_ret = pc+1;
					break;
				case TypeInfo_Map:
					hmap = (Hmap*)b;
					maptype = (MapType*)t;
					if(hash_gciter_init(hmap, &map_iter)) {
						mapkey_size = maptype->key->size;
						mapkey_kind = maptype->key->kind;
						mapkey_ti   = (uintptr)maptype->key->gc | PRECISE;
						mapval_size = maptype->elem->size;
						mapval_kind = maptype->elem->kind;
						mapval_ti   = (uintptr)maptype->elem->gc | PRECISE;

						map_ret = nil;
						pc = mapProg;
					} else {
						goto next_block;
					}
					break;
				case TypeInfo_Chan:
					chan = (Hchan*)b;
					chantype = (ChanType*)t;
					chan_ret = nil;
					pc = chanProg;
					break;
				default:
					runtime_throw("scanblock: invalid type");
					return;
				}
			} else {
				pc = defaultProg;
			}
#endif
		} else {
			pc = defaultProg;
		}

		if(IgnorePreciseGC)
			pc = defaultProg;

		pc++;
		stack_top.b = (uintptr)b;

		end_b = (uintptr)b + n - PtrSize;

	for(;;) {
		if(CollectStats)
			runtime_xadd64(&gcstats.instr[pc[0]], 1);

		obj = nil;
		objti = 0;
		switch(pc[0]) {
		case GC_PTR:
			obj = *(void**)(stack_top.b + pc[1]);
			objti = pc[2];
			pc += 3;
			if(Debug)
				checkptr(obj, objti);
			break;

		case GC_SLICE:
			sliceptr = (Slice*)(stack_top.b + pc[1]);
			if(sliceptr->cap != 0) {
				obj = sliceptr->array;
				// Can't use slice element type for scanning,
				// because if it points to an array embedded
				// in the beginning of a struct,
				// we will scan the whole struct as the slice.
				// So just obtain type info from heap.
			}
			pc += 3;
			break;

		case GC_APTR:
			obj = *(void**)(stack_top.b + pc[1]);
			pc += 2;
			break;

		case GC_STRING:
			obj = *(void**)(stack_top.b + pc[1]);
			markonly(obj);
			pc += 2;
			continue;

		case GC_EFACE:
			eface = (Eface*)(stack_top.b + pc[1]);
			pc += 2;
			if(eface->type == nil)
				continue;

			// eface->type
			t = eface->type;
			if((const byte*)t >= arena_start && (const byte*)t < arena_used) {
				union { const Type *tc; Type *tr; } u;
				u.tc = t;
				*ptrbufpos++ = (struct PtrTarget){(void*)u.tr, 0};
				if(ptrbufpos == ptrbuf_end)
					flushptrbuf(ptrbuf, &ptrbufpos, &wp, &wbuf, &nobj);
			}

			// eface->__object
			if((byte*)eface->__object >= arena_start && (byte*)eface->__object < arena_used) {
				if(t->__size <= sizeof(void*)) {
					if((t->kind & KindNoPointers))
						continue;

					obj = eface->__object;
					if((t->kind & ~KindNoPointers) == KindPtr)
						// objti = (uintptr)((PtrType*)t)->elem->gc;
						objti = 0;
				} else {
					obj = eface->__object;
					// objti = (uintptr)t->gc;
					objti = 0;
				}
			}
			break;

		case GC_IFACE:
			iface = (Iface*)(stack_top.b + pc[1]);
			pc += 2;
			if(iface->tab == nil)
				continue;
			
			// iface->tab
			if((byte*)iface->tab >= arena_start && (byte*)iface->tab < arena_used) {
				// *ptrbufpos++ = (struct PtrTarget){iface->tab, (uintptr)itabtype->gc};
				*ptrbufpos++ = (struct PtrTarget){iface->tab, 0};
				if(ptrbufpos == ptrbuf_end)
					flushptrbuf(ptrbuf, &ptrbufpos, &wp, &wbuf, &nobj);
			}

			// iface->data
			if((byte*)iface->__object >= arena_start && (byte*)iface->__object < arena_used) {
				// t = iface->tab->type;
				t = nil;
				if(t->__size <= sizeof(void*)) {
					if((t->kind & KindNoPointers))
						continue;

					obj = iface->__object;
					if((t->kind & ~KindNoPointers) == KindPtr)
						// objti = (uintptr)((const PtrType*)t)->elem->gc;
						objti = 0;
				} else {
					obj = iface->__object;
					// objti = (uintptr)t->gc;
					objti = 0;
				}
			}
			break;

		case GC_DEFAULT_PTR:
			while(stack_top.b <= end_b) {
				obj = *(byte**)stack_top.b;
				stack_top.b += PtrSize;
				if((byte*)obj >= arena_start && (byte*)obj < arena_used) {
					*ptrbufpos++ = (struct PtrTarget){obj, 0};
					if(ptrbufpos == ptrbuf_end)
						flushptrbuf(ptrbuf, &ptrbufpos, &wp, &wbuf, &nobj);
				}
			}
			goto next_block;

		case GC_END:
			if(--stack_top.count != 0) {
				// Next iteration of a loop if possible.
				stack_top.b += stack_top.elemsize;
				if(stack_top.b + stack_top.elemsize <= end_b+PtrSize) {
					pc = stack_top.loop_or_ret;
					continue;
				}
				i = stack_top.b;
			} else {
				// Stack pop if possible.
				if(stack_ptr+1 < stack+nelem(stack)) {
					pc = stack_top.loop_or_ret;
					stack_top = *(++stack_ptr);
					continue;
				}
				i = (uintptr)b + nominal_size;
			}
			if(!precise_type) {
				// Quickly scan [b+i,b+n) for possible pointers.
				for(; i<=end_b; i+=PtrSize) {
					if(*(byte**)i != nil) {
						// Found a value that may be a pointer.
						// Do a rescan of the entire block.
						enqueue((Obj){b, n, 0}, &wbuf, &wp, &nobj);
						if(CollectStats) {
							runtime_xadd64(&gcstats.rescan, 1);
							runtime_xadd64(&gcstats.rescanbytes, n);
						}
						break;
					}
				}
			}
			goto next_block;

		case GC_ARRAY_START:
			i = stack_top.b + pc[1];
			count = pc[2];
			elemsize = pc[3];
			pc += 4;

			// Stack push.
			*stack_ptr-- = stack_top;
			stack_top = (Frame){count, elemsize, i, pc};
			continue;

		case GC_ARRAY_NEXT:
			if(--stack_top.count != 0) {
				stack_top.b += stack_top.elemsize;
				pc = stack_top.loop_or_ret;
			} else {
				// Stack pop.
				stack_top = *(++stack_ptr);
				pc += 1;
			}
			continue;

		case GC_CALL:
			// Stack push.
			*stack_ptr-- = stack_top;
			stack_top = (Frame){1, 0, stack_top.b + pc[1], pc+3 /*return address*/};
			pc = (uintptr*)((byte*)pc + *(int32*)(pc+2));  // target of the CALL instruction
			continue;

#if 0
		case GC_MAP_PTR:
			hmap = *(Hmap**)(stack_top.b + pc[1]);
			if(hmap == nil) {
				pc += 3;
				continue;
			}
			if(markonly(hmap)) {
				maptype = (MapType*)pc[2];
				if(hash_gciter_init(hmap, &map_iter)) {
					mapkey_size = maptype->key->size;
					mapkey_kind = maptype->key->kind;
					mapkey_ti   = (uintptr)maptype->key->gc | PRECISE;
					mapval_size = maptype->elem->size;
					mapval_kind = maptype->elem->kind;
					mapval_ti   = (uintptr)maptype->elem->gc | PRECISE;

					// Start mapProg.
					map_ret = pc+3;
					pc = mapProg+1;
				} else {
					pc += 3;
				}
			} else {
				pc += 3;
			}
			continue;

		case GC_MAP_NEXT:
			// Add all keys and values to buffers, mark all subtables.
			while(hash_gciter_next(&map_iter, &d)) {
				// buffers: reserve space for 2 objects.
				if(ptrbufpos+2 >= ptrbuf_end)
					flushptrbuf(ptrbuf, &ptrbufpos, &wp, &wbuf, &nobj);
				if(objbufpos+2 >= objbuf_end)
					flushobjbuf(objbuf, &objbufpos, &wp, &wbuf, &nobj);

				if(d.st != nil)
					markonly(d.st);

				if(d.key_data != nil) {
					if(!(mapkey_kind & KindNoPointers) || d.indirectkey) {
						if(!d.indirectkey)
							*objbufpos++ = (Obj){d.key_data, mapkey_size, mapkey_ti};
						else {
							if(Debug) {
								obj = *(void**)d.key_data;
								if(!(arena_start <= obj && obj < arena_used))
									runtime_throw("scanblock: inconsistent hashmap");
							}
							*ptrbufpos++ = (struct PtrTarget){*(void**)d.key_data, mapkey_ti};
						}
					}
					if(!(mapval_kind & KindNoPointers) || d.indirectval) {
						if(!d.indirectval)
							*objbufpos++ = (Obj){d.val_data, mapval_size, mapval_ti};
						else {
							if(Debug) {
								obj = *(void**)d.val_data;
								if(!(arena_start <= obj && obj < arena_used))
									runtime_throw("scanblock: inconsistent hashmap");
							}
							*ptrbufpos++ = (struct PtrTarget){*(void**)d.val_data, mapval_ti};
						}
					}
				}
			}
			if(map_ret == nil)
				goto next_block;
			pc = map_ret;
			continue;
#endif

		case GC_REGION:
			obj = (void*)(stack_top.b + pc[1]);
			size = pc[2];
			objti = pc[3];
			pc += 4;

			*objbufpos++ = (Obj){obj, size, objti};
			if(objbufpos == objbuf_end)
				flushobjbuf(objbuf, &objbufpos, &wp, &wbuf, &nobj);
			continue;

#if 0
		case GC_CHAN_PTR:
			// Similar to GC_MAP_PTR
			chan = *(Hchan**)(stack_top.b + pc[1]);
			if(chan == nil) {
				pc += 3;
				continue;
			}
			if(markonly(chan)) {
				chantype = (ChanType*)pc[2];
				if(!(chantype->elem->kind & KindNoPointers)) {
					// Start chanProg.
					chan_ret = pc+3;
					pc = chanProg+1;
					continue;
				}
			}
			pc += 3;
			continue;

		case GC_CHAN:
			// There are no heap pointers in struct Hchan,
			// so we can ignore the leading sizeof(Hchan) bytes.
			if(!(chantype->elem->kind & KindNoPointers)) {
				// Channel's buffer follows Hchan immediately in memory.
				// Size of buffer (cap(c)) is second int in the chan struct.
				chancap = ((uintgo*)chan)[1];
				if(chancap > 0) {
					// TODO(atom): split into two chunks so that only the
					// in-use part of the circular buffer is scanned.
					// (Channel routines zero the unused part, so the current
					// code does not lead to leaks, it's just a little inefficient.)
					*objbufpos++ = (Obj){(byte*)chan+runtime_Hchansize, chancap*chantype->elem->size,
						(uintptr)chantype->elem->gc | PRECISE | LOOP};
					if(objbufpos == objbuf_end)
						flushobjbuf(objbuf, &objbufpos, &wp, &wbuf, &nobj);
				}
			}
			if(chan_ret == nil)
				goto next_block;
			pc = chan_ret;
			continue;
#endif

		default:
			runtime_throw("scanblock: invalid GC instruction");
			return;
		}

		if((byte*)obj >= arena_start && (byte*)obj < arena_used) {
			*ptrbufpos++ = (struct PtrTarget){obj, objti};
			if(ptrbufpos == ptrbuf_end)
				flushptrbuf(ptrbuf, &ptrbufpos, &wp, &wbuf, &nobj);
		}
	}

	next_block:
		// Done scanning [b, b+n).  Prepare for the next iteration of
		// the loop by setting b, n, ti to the parameters for the next block.

		if(nobj == 0) {
			flushptrbuf(ptrbuf, &ptrbufpos, &wp, &wbuf, &nobj);
			flushobjbuf(objbuf, &objbufpos, &wp, &wbuf, &nobj);

			if(nobj == 0) {
				if(!keepworking) {
					if(wbuf)
						putempty(wbuf);
					goto endscan;
				}
				// Emptied our buffer: refill.
				wbuf = getfull(wbuf);
				if(wbuf == nil)
					goto endscan;
				nobj = wbuf->nobj;
				wp = wbuf->obj + wbuf->nobj;
			}
		}

		// Fetch b from the work buffer.
		--wp;
		b = wp->p;
		n = wp->n;
		ti = wp->ti;
		nobj--;
	}

endscan:;
}

// debug_scanblock is the debug copy of scanblock.
// it is simpler, slower, single-threaded, recursive,
// and uses bitSpecial as the mark bit.
static void
debug_scanblock(byte *b, uintptr n)
{
	byte *obj, *p;
	void **vp;
	uintptr size, *bitp, bits, shift, i, xbits, off;
	MSpan *s;

	if(!DebugMark)
		runtime_throw("debug_scanblock without DebugMark");

	if((intptr)n < 0) {
		runtime_printf("debug_scanblock %p %D\n", b, (int64)n);
		runtime_throw("debug_scanblock");
	}

	// Align b to a word boundary.
	off = (uintptr)b & (PtrSize-1);
	if(off != 0) {
		b += PtrSize - off;
		n -= PtrSize - off;
	}

	vp = (void**)b;
	n /= PtrSize;
	for(i=0; i<(uintptr)n; i++) {
		obj = (byte*)vp[i];

		// Words outside the arena cannot be pointers.
		if((byte*)obj < runtime_mheap->arena_start || (byte*)obj >= runtime_mheap->arena_used)
			continue;

		// Round down to word boundary.
		obj = (void*)((uintptr)obj & ~((uintptr)PtrSize-1));

		// Consult span table to find beginning.
		s = runtime_MHeap_LookupMaybe(runtime_mheap, obj);
		if(s == nil)
			continue;

		p =  (byte*)((uintptr)s->start<<PageShift);
		size = s->elemsize;
		if(s->sizeclass == 0) {
			obj = p;
		} else {
			if((byte*)obj >= (byte*)s->limit)
				continue;
			int32 i = ((byte*)obj - p)/size;
			obj = p+i*size;
		}

		// Now that we know the object header, reload bits.
		off = (uintptr*)obj - (uintptr*)runtime_mheap->arena_start;
		bitp = (uintptr*)runtime_mheap->arena_start - off/wordsPerBitmapWord - 1;
		shift = off % wordsPerBitmapWord;
		xbits = *bitp;
		bits = xbits >> shift;

		// Now we have bits, bitp, and shift correct for
		// obj pointing at the base of the object.
		// If not allocated or already marked, done.
		if((bits & bitAllocated) == 0 || (bits & bitSpecial) != 0)  // NOTE: bitSpecial not bitMarked
			continue;
		*bitp |= bitSpecial<<shift;
		if(!(bits & bitMarked))
			runtime_printf("found unmarked block %p in %p\n", obj, vp+i);

		// If object has no pointers, don't need to scan further.
		if((bits & bitNoPointers) != 0)
			continue;

		debug_scanblock(obj, size);
	}
}

// Append obj to the work buffer.
// _wbuf, _wp, _nobj are input/output parameters and are specifying the work buffer.
static void
enqueue(Obj obj, Workbuf **_wbuf, Obj **_wp, uintptr *_nobj)
{
	uintptr nobj, off;
	Obj *wp;
	Workbuf *wbuf;

	if(Debug > 1)
		runtime_printf("append obj(%p %D %p)\n", obj.p, (int64)obj.n, obj.ti);

	// Align obj.b to a word boundary.
	off = (uintptr)obj.p & (PtrSize-1);
	if(off != 0) {
		obj.p += PtrSize - off;
		obj.n -= PtrSize - off;
		obj.ti = 0;
	}

	if(obj.p == nil || obj.n == 0)
		return;

	// Load work buffer state
	wp = *_wp;
	wbuf = *_wbuf;
	nobj = *_nobj;

	// If another proc wants a pointer, give it some.
	if(work.nwait > 0 && nobj > handoffThreshold && work.full == 0) {
		wbuf->nobj = nobj;
		wbuf = handoff(wbuf);
		nobj = wbuf->nobj;
		wp = wbuf->obj + nobj;
	}

	// If buffer is full, get a new one.
	if(wbuf == nil || nobj >= nelem(wbuf->obj)) {
		if(wbuf != nil)
			wbuf->nobj = nobj;
		wbuf = getempty(wbuf);
		wp = wbuf->obj;
		nobj = 0;
	}

	*wp = obj;
	wp++;
	nobj++;

	// Save work buffer state
	*_wp = wp;
	*_wbuf = wbuf;
	*_nobj = nobj;
}

static void
markroot(ParFor *desc, uint32 i)
{
	Obj *wp;
	Workbuf *wbuf;
	uintptr nobj;

	USED(&desc);
	wp = nil;
	wbuf = nil;
	nobj = 0;
	enqueue(work.roots[i], &wbuf, &wp, &nobj);
	scanblock(wbuf, wp, nobj, false);
}

// Get an empty work buffer off the work.empty list,
// allocating new buffers as needed.
static Workbuf*
getempty(Workbuf *b)
{
	if(b != nil)
		runtime_lfstackpush(&work.full, &b->node);
	b = (Workbuf*)runtime_lfstackpop(&work.empty);
	if(b == nil) {
		// Need to allocate.
		runtime_lock(&work);
		if(work.nchunk < sizeof *b) {
			work.nchunk = 1<<20;
			work.chunk = runtime_SysAlloc(work.nchunk);
			if(work.chunk == nil)
				runtime_throw("runtime: cannot allocate memory");
		}
		b = (Workbuf*)work.chunk;
		work.chunk += sizeof *b;
		work.nchunk -= sizeof *b;
		runtime_unlock(&work);
	}
	b->nobj = 0;
	return b;
}

static void
putempty(Workbuf *b)
{
	if(CollectStats)
		runtime_xadd64(&gcstats.putempty, 1);

	runtime_lfstackpush(&work.empty, &b->node);
}

// Get a full work buffer off the work.full list, or return nil.
static Workbuf*
getfull(Workbuf *b)
{
	M *m;
	int32 i;

	if(CollectStats)
		runtime_xadd64(&gcstats.getfull, 1);

	if(b != nil)
		runtime_lfstackpush(&work.empty, &b->node);
	b = (Workbuf*)runtime_lfstackpop(&work.full);
	if(b != nil || work.nproc == 1)
		return b;

	m = runtime_m();
	runtime_xadd(&work.nwait, +1);
	for(i=0;; i++) {
		if(work.full != 0) {
			runtime_xadd(&work.nwait, -1);
			b = (Workbuf*)runtime_lfstackpop(&work.full);
			if(b != nil)
				return b;
			runtime_xadd(&work.nwait, +1);
		}
		if(work.nwait == work.nproc)
			return nil;
		if(i < 10) {
			m->gcstats.nprocyield++;
			runtime_procyield(20);
		} else if(i < 20) {
			m->gcstats.nosyield++;
			runtime_osyield();
		} else {
			m->gcstats.nsleep++;
			runtime_usleep(100);
		}
	}
}

static Workbuf*
handoff(Workbuf *b)
{
	M *m;
	int32 n;
	Workbuf *b1;

	m = runtime_m();

	// Make new buffer with half of b's pointers.
	b1 = getempty(nil);
	n = b->nobj/2;
	b->nobj -= n;
	b1->nobj = n;
	runtime_memmove(b1->obj, b->obj+b->nobj, n*sizeof b1->obj[0]);
	m->gcstats.nhandoff++;
	m->gcstats.nhandoffcnt += n;

	// Put b on full list - let first half of b get stolen.
	runtime_lfstackpush(&work.full, &b->node);
	return b1;
}

static void
addroot(Obj obj)
{
	uint32 cap;
	Obj *new;

	if(work.nroot >= work.rootcap) {
		cap = PageSize/sizeof(Obj);
		if(cap < 2*work.rootcap)
			cap = 2*work.rootcap;
		new = (Obj*)runtime_SysAlloc(cap*sizeof(Obj));
		if(new == nil)
			runtime_throw("runtime: cannot allocate memory");
		if(work.roots != nil) {
			runtime_memmove(new, work.roots, work.rootcap*sizeof(Obj));
			runtime_SysFree(work.roots, work.rootcap*sizeof(Obj));
		}
		work.roots = new;
		work.rootcap = cap;
	}
	work.roots[work.nroot] = obj;
	work.nroot++;
}

static void
addstackroots(G *gp)
{
#ifdef USING_SPLIT_STACK
	M *mp;
	void* sp;
	size_t spsize;
	void* next_segment;
	void* next_sp;
	void* initial_sp;

	if(gp == runtime_g()) {
		// Scanning our own stack.
		sp = __splitstack_find(nil, nil, &spsize, &next_segment,
				       &next_sp, &initial_sp);
	} else if((mp = gp->m) != nil && mp->helpgc) {
		// gchelper's stack is in active use and has no interesting pointers.
		return;
	} else {
		// Scanning another goroutine's stack.
		// The goroutine is usually asleep (the world is stopped).

		// The exception is that if the goroutine is about to enter or might
		// have just exited a system call, it may be executing code such
		// as schedlock and may have needed to start a new stack segment.
		// Use the stack segment and stack pointer at the time of
		// the system call instead, since that won't change underfoot.
		if(gp->gcstack != nil) {
			sp = gp->gcstack;
			spsize = gp->gcstack_size;
			next_segment = gp->gcnext_segment;
			next_sp = gp->gcnext_sp;
			initial_sp = gp->gcinitial_sp;
		} else {
			sp = __splitstack_find_context(&gp->stack_context[0],
						       &spsize, &next_segment,
						       &next_sp, &initial_sp);
		}
	}
	if(sp != nil) {
		addroot((Obj){sp, spsize, 0});
		while((sp = __splitstack_find(next_segment, next_sp,
					      &spsize, &next_segment,
					      &next_sp, &initial_sp)) != nil)
			addroot((Obj){sp, spsize, 0});
	}
#else
	M *mp;
	byte* bottom;
	byte* top;

	if(gp == runtime_g()) {
		// Scanning our own stack.
		bottom = (byte*)&gp;
	} else if((mp = gp->m) != nil && mp->helpgc) {
		// gchelper's stack is in active use and has no interesting pointers.
		return;
	} else {
		// Scanning another goroutine's stack.
		// The goroutine is usually asleep (the world is stopped).
		bottom = (byte*)gp->gcnext_sp;
		if(bottom == nil)
			return;
	}
	top = (byte*)gp->gcinitial_sp + gp->gcstack_size;
	if(top > bottom)
		addroot((Obj){bottom, top - bottom, 0});
	else
		addroot((Obj){top, bottom - top, 0});
#endif
}

static void
addfinroots(void *v)
{
	uintptr size;
	void *base;

	size = 0;
	if(!runtime_mlookup(v, (byte**)&base, &size, nil) || !runtime_blockspecial(base))
		runtime_throw("mark - finalizer inconsistency");

	// do not mark the finalizer block itself.  just mark the things it points at.
	addroot((Obj){base, size, 0});
}

static struct root_list* roots;

void
__go_register_gc_roots (struct root_list* r)
{
	// FIXME: This needs locking if multiple goroutines can call
	// dlopen simultaneously.
	r->next = roots;
	roots = r;
}

static void
addroots(void)
{
	struct root_list *pl;
	G *gp;
	FinBlock *fb;
	MSpan *s, **allspans;
	uint32 spanidx;

	work.nroot = 0;

	// mark data+bss.
	for(pl = roots; pl != nil; pl = pl->next) {
		struct root* pr = &pl->roots[0];
		while(1) {
			void *decl = pr->decl;
			if(decl == nil)
				break;
			addroot((Obj){decl, pr->size, 0});
			pr++;
		}
	}

	addroot((Obj){(byte*)&runtime_m0, sizeof runtime_m0, 0});
	addroot((Obj){(byte*)&runtime_g0, sizeof runtime_g0, 0});
	addroot((Obj){(byte*)&runtime_allg, sizeof runtime_allg, 0});
	addroot((Obj){(byte*)&runtime_allm, sizeof runtime_allm, 0});
	addroot((Obj){(byte*)&runtime_allp, sizeof runtime_allp, 0});
	runtime_proc_scan(addroot);
	runtime_MProf_Mark(addroot);
	runtime_time_scan(addroot);

	// MSpan.types
	allspans = runtime_mheap->allspans;
	for(spanidx=0; spanidx<runtime_mheap->nspan; spanidx++) {
		s = allspans[spanidx];
		if(s->state == MSpanInUse) {
			// The garbage collector ignores type pointers stored in MSpan.types:
			//  - Compiler-generated types are stored outside of heap.
			//  - The reflect package has runtime-generated types cached in its data structures.
			//    The garbage collector relies on finding the references via that cache.
			switch(s->types.compression) {
			case MTypes_Empty:
			case MTypes_Single:
				break;
			case MTypes_Words:
			case MTypes_Bytes:
				markonly((byte*)s->types.data);
				break;
			}
		}
	}

	// stacks
	for(gp=runtime_allg; gp!=nil; gp=gp->alllink) {
		switch(gp->status){
		default:
			runtime_printf("unexpected G.status %d\n", gp->status);
			runtime_throw("mark - bad status");
		case Gdead:
			break;
		case Grunning:
			if(gp != runtime_g())
				runtime_throw("mark - world not stopped");
			addstackroots(gp);
			break;
		case Grunnable:
		case Gsyscall:
		case Gwaiting:
			addstackroots(gp);
			break;
		}
	}

	runtime_walkfintab(addfinroots, addroot);

	for(fb=allfin; fb; fb=fb->alllink)
		addroot((Obj){(byte*)fb->fin, fb->cnt*sizeof(fb->fin[0]), 0});

	addroot((Obj){(byte*)&work, sizeof work, 0});
}

static bool
handlespecial(byte *p, uintptr size)
{
	FuncVal *fn;
	const struct __go_func_type *ft;
	FinBlock *block;
	Finalizer *f;
	
	if(!runtime_getfinalizer(p, true, &fn, &ft)) {
		runtime_setblockspecial(p, false);
		runtime_MProf_Free(p, size);
		return false;
	}

	runtime_lock(&finlock);
	if(finq == nil || finq->cnt == finq->cap) {
		if(finc == nil) {
			finc = runtime_SysAlloc(PageSize);
			if(finc == nil)
				runtime_throw("runtime: cannot allocate memory");
			finc->cap = (PageSize - sizeof(FinBlock)) / sizeof(Finalizer) + 1;
			finc->alllink = allfin;
			allfin = finc;
		}
		block = finc;
		finc = block->next;
		block->next = finq;
		finq = block;
	}
	f = &finq->fin[finq->cnt];
	finq->cnt++;
	f->fn = fn;
	f->ft = ft;
	f->arg = p;
	runtime_unlock(&finlock);
	return true;
}

// Sweep frees or collects finalizers for blocks not marked in the mark phase.
// It clears the mark bits in preparation for the next GC round.
static void
sweepspan(ParFor *desc, uint32 idx)
{
	M *m;
	int32 cl, n, npages;
	uintptr size;
	byte *p;
	MCache *c;
	byte *arena_start;
	MLink head, *end;
	int32 nfree;
	byte *type_data;
	byte compression;
	uintptr type_data_inc;
	MSpan *s;

	m = runtime_m();

	USED(&desc);
	s = runtime_mheap->allspans[idx];
	if(s->state != MSpanInUse)
		return;
	arena_start = runtime_mheap->arena_start;
	p = (byte*)(s->start << PageShift);
	cl = s->sizeclass;
	size = s->elemsize;
	if(cl == 0) {
		n = 1;
	} else {
		// Chunk full of small blocks.
		npages = runtime_class_to_allocnpages[cl];
		n = (npages << PageShift) / size;
	}
	nfree = 0;
	end = &head;
	c = m->mcache;
	
	type_data = (byte*)s->types.data;
	type_data_inc = sizeof(uintptr);
	compression = s->types.compression;
	switch(compression) {
	case MTypes_Bytes:
		type_data += 8*sizeof(uintptr);
		type_data_inc = 1;
		break;
	}

	// Sweep through n objects of given size starting at p.
	// This thread owns the span now, so it can manipulate
	// the block bitmap without atomic operations.
	for(; n > 0; n--, p += size, type_data+=type_data_inc) {
		uintptr off, *bitp, shift, bits;

		off = (uintptr*)p - (uintptr*)arena_start;
		bitp = (uintptr*)arena_start - off/wordsPerBitmapWord - 1;
		shift = off % wordsPerBitmapWord;
		bits = *bitp>>shift;

		if((bits & bitAllocated) == 0)
			continue;

		if((bits & bitMarked) != 0) {
			if(DebugMark) {
				if(!(bits & bitSpecial))
					runtime_printf("found spurious mark on %p\n", p);
				*bitp &= ~(bitSpecial<<shift);
			}
			*bitp &= ~(bitMarked<<shift);
			continue;
		}

		// Special means it has a finalizer or is being profiled.
		// In DebugMark mode, the bit has been coopted so
		// we have to assume all blocks are special.
		if(DebugMark || (bits & bitSpecial) != 0) {
			if(handlespecial(p, size))
				continue;
		}

		// Mark freed; restore block boundary bit.
		*bitp = (*bitp & ~(bitMask<<shift)) | (bitBlockBoundary<<shift);

		if(cl == 0) {
			// Free large span.
			runtime_unmarkspan(p, 1<<PageShift);
			*(uintptr*)p = (uintptr)0xdeaddeaddeaddeadll;	// needs zeroing
			runtime_MHeap_Free(runtime_mheap, s, 1);
			c->local_alloc -= size;
			c->local_nfree++;
		} else {
			// Free small object.
			switch(compression) {
			case MTypes_Words:
				*(uintptr*)type_data = 0;
				break;
			case MTypes_Bytes:
				*(byte*)type_data = 0;
				break;
			}
			if(size > sizeof(uintptr))
				((uintptr*)p)[1] = (uintptr)0xdeaddeaddeaddeadll;	// mark as "needs to be zeroed"
			
			end->next = (MLink*)p;
			end = (MLink*)p;
			nfree++;
		}
	}

	if(nfree) {
		c->local_by_size[cl].nfree += nfree;
		c->local_alloc -= size * nfree;
		c->local_nfree += nfree;
		c->local_cachealloc -= nfree * size;
		c->local_objects -= nfree;
		runtime_MCentral_FreeSpan(&runtime_mheap->central[cl], s, nfree, head.next, end);
	}
}

static void
dumpspan(uint32 idx)
{
	int32 sizeclass, n, npages, i, column;
	uintptr size;
	byte *p;
	byte *arena_start;
	MSpan *s;
	bool allocated, special;

	s = runtime_mheap->allspans[idx];
	if(s->state != MSpanInUse)
		return;
	arena_start = runtime_mheap->arena_start;
	p = (byte*)(s->start << PageShift);
	sizeclass = s->sizeclass;
	size = s->elemsize;
	if(sizeclass == 0) {
		n = 1;
	} else {
		npages = runtime_class_to_allocnpages[sizeclass];
		n = (npages << PageShift) / size;
	}
	
	runtime_printf("%p .. %p:\n", p, p+n*size);
	column = 0;
	for(; n>0; n--, p+=size) {
		uintptr off, *bitp, shift, bits;

		off = (uintptr*)p - (uintptr*)arena_start;
		bitp = (uintptr*)arena_start - off/wordsPerBitmapWord - 1;
		shift = off % wordsPerBitmapWord;
		bits = *bitp>>shift;

		allocated = ((bits & bitAllocated) != 0);
		special = ((bits & bitSpecial) != 0);

		for(i=0; (uint32)i<size; i+=sizeof(void*)) {
			if(column == 0) {
				runtime_printf("\t");
			}
			if(i == 0) {
				runtime_printf(allocated ? "(" : "[");
				runtime_printf(special ? "@" : "");
				runtime_printf("%p: ", p+i);
			} else {
				runtime_printf(" ");
			}

			runtime_printf("%p", *(void**)(p+i));

			if(i+sizeof(void*) >= size) {
				runtime_printf(allocated ? ") " : "] ");
			}

			column++;
			if(column == 8) {
				runtime_printf("\n");
				column = 0;
			}
		}
	}
	runtime_printf("\n");
}

// A debugging function to dump the contents of memory
void
runtime_memorydump(void)
{
	uint32 spanidx;

	for(spanidx=0; spanidx<runtime_mheap->nspan; spanidx++) {
		dumpspan(spanidx);
	}
}

void
runtime_gchelper(void)
{
	gchelperstart();

	// parallel mark for over gc roots
	runtime_parfordo(work.markfor);

	// help other threads scan secondary blocks
	scanblock(nil, nil, 0, true);

	if(DebugMark) {
		// wait while the main thread executes mark(debug_scanblock)
		while(runtime_atomicload(&work.debugmarkdone) == 0)
			runtime_usleep(10);
	}

	runtime_parfordo(work.sweepfor);
	bufferList[runtime_m()->helpgc].busy = 0;
	if(runtime_xadd(&work.ndone, +1) == work.nproc-1)
		runtime_notewakeup(&work.alldone);
}

#define GcpercentUnknown (-2)

// Initialized from $GOGC.  GOGC=off means no gc.
//
// Next gc is after we've allocated an extra amount of
// memory proportional to the amount already in use.
// If gcpercent=100 and we're using 4M, we'll gc again
// when we get to 8M.  This keeps the gc cost in linear
// proportion to the allocation cost.  Adjusting gcpercent
// just changes the linear constant (and also the amount of
// extra memory used).
static int32 gcpercent = GcpercentUnknown;

static void
cachestats(GCStats *stats)
{
	M *mp;
	MCache *c;
	P *p, **pp;
	uint32 i;
	uint64 stacks_inuse;
	uint64 *src, *dst;

	if(stats)
		runtime_memclr((byte*)stats, sizeof(*stats));
	stacks_inuse = 0;
	for(mp=runtime_allm; mp; mp=mp->alllink) {
		//stacks_inuse += mp->stackinuse*FixedStack;
		if(stats) {
			src = (uint64*)&mp->gcstats;
			dst = (uint64*)stats;
			for(i=0; i<sizeof(*stats)/sizeof(uint64); i++)
				dst[i] += src[i];
			runtime_memclr((byte*)&mp->gcstats, sizeof(mp->gcstats));
		}
	}
	for(pp=runtime_allp; (p=*pp) != nil; pp++) {
		c = p->mcache;
		if(c==nil)
			continue;
		runtime_purgecachedstats(c);
		for(i=0; i<nelem(c->local_by_size); i++) {
			mstats.by_size[i].nmalloc += c->local_by_size[i].nmalloc;
			c->local_by_size[i].nmalloc = 0;
			mstats.by_size[i].nfree += c->local_by_size[i].nfree;
			c->local_by_size[i].nfree = 0;
		}
	}
	mstats.stacks_inuse = stacks_inuse;
}

// Structure of arguments passed to function gc().
// This allows the arguments to be passed via reflect_call.
struct gc_args
{
	int32 force;
};

static void gc(struct gc_args *args);

static int32
readgogc(void)
{
	const byte *p;

	p = runtime_getenv("GOGC");
	if(p == nil || p[0] == '\0')
		return 100;
	if(runtime_strcmp((const char *)p, "off") == 0)
		return -1;
	return runtime_atoi(p);
}

void
runtime_gc(int32 force)
{
	M *m;
	const byte *p;
	struct gc_args a, *ap;

	// The atomic operations are not atomic if the uint64s
	// are not aligned on uint64 boundaries. This has been
	// a problem in the past.
	if((((uintptr)&work.empty) & 7) != 0)
		runtime_throw("runtime: gc work buffer is misaligned");
	if((((uintptr)&work.full) & 7) != 0)
		runtime_throw("runtime: gc work buffer is misaligned");

	// Make sure all registers are saved on stack so that
	// scanstack sees them.
	__builtin_unwind_init();

	// The gc is turned off (via enablegc) until
	// the bootstrap has completed.
	// Also, malloc gets called in the guts
	// of a number of libraries that might be
	// holding locks.  To avoid priority inversion
	// problems, don't bother trying to run gc
	// while holding a lock.  The next mallocgc
	// without a lock will do the gc instead.
	m = runtime_m();
	if(!mstats.enablegc || m->locks > 0 || runtime_panicking)
		return;

	if(gcpercent == GcpercentUnknown) {	// first time through
		gcpercent = readgogc();

		p = runtime_getenv("GOGCTRACE");
		if(p != nil)
			gctrace = runtime_atoi(p);
	}
	if(gcpercent < 0)
		return;

	// Run gc on a bigger stack to eliminate
	// a potentially large number of calls to runtime_morestack.
	// But not when using gccgo.
	a.force = force;
	ap = &a;
	gc(ap);

	if(gctrace > 1 && !force) {
		a.force = 1;
		gc(&a);
	}
}

static void
gc(struct gc_args *args)
{
	M *m;
	int64 t0, t1, t2, t3, t4;
	uint64 heap0, heap1, obj0, obj1, ninstr;
	GCStats stats;
	M *mp;
	uint32 i;
	// Eface eface;

	runtime_semacquire(&runtime_worldsema);
	if(!args->force && mstats.heap_alloc < mstats.next_gc) {
		runtime_semrelease(&runtime_worldsema);
		return;
	}

	m = runtime_m();

	t0 = runtime_nanotime();

	m->gcing = 1;
	runtime_stoptheworld();

	if(CollectStats)
		runtime_memclr((byte*)&gcstats, sizeof(gcstats));

	for(mp=runtime_allm; mp; mp=mp->alllink)
		runtime_settype_flush(mp, false);

	heap0 = 0;
	obj0 = 0;
	if(gctrace) {
		cachestats(nil);
		heap0 = mstats.heap_alloc;
		obj0 = mstats.nmalloc - mstats.nfree;
	}

	m->locks++;	// disable gc during mallocs in parforalloc
	if(work.markfor == nil)
		work.markfor = runtime_parforalloc(MaxGcproc);
	if(work.sweepfor == nil)
		work.sweepfor = runtime_parforalloc(MaxGcproc);
	m->locks--;

	if(itabtype == nil) {
		// get C pointer to the Go type "itab"
		// runtime_gc_itab_ptr(&eface);
		// itabtype = ((PtrType*)eface.type)->elem;
	}

	work.nwait = 0;
	work.ndone = 0;
	work.debugmarkdone = 0;
	work.nproc = runtime_gcprocs();
	addroots();
	runtime_parforsetup(work.markfor, work.nproc, work.nroot, nil, false, markroot);
	runtime_parforsetup(work.sweepfor, work.nproc, runtime_mheap->nspan, nil, true, sweepspan);
	if(work.nproc > 1) {
		runtime_noteclear(&work.alldone);
		runtime_helpgc(work.nproc);
	}

	t1 = runtime_nanotime();

	gchelperstart();
	runtime_parfordo(work.markfor);
	scanblock(nil, nil, 0, true);

	if(DebugMark) {
		for(i=0; i<work.nroot; i++)
			debug_scanblock(work.roots[i].p, work.roots[i].n);
		runtime_atomicstore(&work.debugmarkdone, 1);
	}
	t2 = runtime_nanotime();

	runtime_parfordo(work.sweepfor);
	bufferList[m->helpgc].busy = 0;
	t3 = runtime_nanotime();

	if(work.nproc > 1)
		runtime_notesleep(&work.alldone);

	cachestats(&stats);

	stats.nprocyield += work.sweepfor->nprocyield;
	stats.nosyield += work.sweepfor->nosyield;
	stats.nsleep += work.sweepfor->nsleep;

	mstats.next_gc = mstats.heap_alloc+(mstats.heap_alloc-runtime_stacks_sys)*gcpercent/100;
	m->gcing = 0;

	if(finq != nil) {
		m->locks++;	// disable gc during the mallocs in newproc
		// kick off or wake up goroutine to run queued finalizers
		if(fing == nil)
			fing = __go_go(runfinq, nil);
		else if(fingwait) {
			fingwait = 0;
			runtime_ready(fing);
		}
		m->locks--;
	}

	heap1 = mstats.heap_alloc;
	obj1 = mstats.nmalloc - mstats.nfree;

	t4 = runtime_nanotime();
	mstats.last_gc = t4;
	mstats.pause_ns[mstats.numgc%nelem(mstats.pause_ns)] = t4 - t0;
	mstats.pause_total_ns += t4 - t0;
	mstats.numgc++;
	if(mstats.debuggc)
		runtime_printf("pause %D\n", t4-t0);

	if(gctrace) {
		runtime_printf("gc%d(%d): %D+%D+%D ms, %D -> %D MB %D -> %D (%D-%D) objects,"
				" %D(%D) handoff, %D(%D) steal, %D/%D/%D yields\n",
			mstats.numgc, work.nproc, (t2-t1)/1000000, (t3-t2)/1000000, (t1-t0+t4-t3)/1000000,
			heap0>>20, heap1>>20, obj0, obj1,
			mstats.nmalloc, mstats.nfree,
			stats.nhandoff, stats.nhandoffcnt,
			work.sweepfor->nsteal, work.sweepfor->nstealcnt,
			stats.nprocyield, stats.nosyield, stats.nsleep);
		if(CollectStats) {
			runtime_printf("scan: %D bytes, %D objects, %D untyped, %D types from MSpan\n",
				gcstats.nbytes, gcstats.obj.cnt, gcstats.obj.notype, gcstats.obj.typelookup);
			if(gcstats.ptr.cnt != 0)
				runtime_printf("avg ptrbufsize: %D (%D/%D)\n",
					gcstats.ptr.sum/gcstats.ptr.cnt, gcstats.ptr.sum, gcstats.ptr.cnt);
			if(gcstats.obj.cnt != 0)
				runtime_printf("avg nobj: %D (%D/%D)\n",
					gcstats.obj.sum/gcstats.obj.cnt, gcstats.obj.sum, gcstats.obj.cnt);
			runtime_printf("rescans: %D, %D bytes\n", gcstats.rescan, gcstats.rescanbytes);

			runtime_printf("instruction counts:\n");
			ninstr = 0;
			for(i=0; i<nelem(gcstats.instr); i++) {
				runtime_printf("\t%d:\t%D\n", i, gcstats.instr[i]);
				ninstr += gcstats.instr[i];
			}
			runtime_printf("\ttotal:\t%D\n", ninstr);

			runtime_printf("putempty: %D, getfull: %D\n", gcstats.putempty, gcstats.getfull);
		}
	}

	runtime_MProf_GC();
	runtime_semrelease(&runtime_worldsema);
	runtime_starttheworld();

	// give the queued finalizers, if any, a chance to run
	if(finq != nil)
		runtime_gosched();
}

void runtime_ReadMemStats(MStats *)
  __asm__ (GOSYM_PREFIX "runtime.ReadMemStats");

void
runtime_ReadMemStats(MStats *stats)
{
	M *m;

	// Have to acquire worldsema to stop the world,
	// because stoptheworld can only be used by
	// one goroutine at a time, and there might be
	// a pending garbage collection already calling it.
	runtime_semacquire(&runtime_worldsema);
	m = runtime_m();
	m->gcing = 1;
	runtime_stoptheworld();
	cachestats(nil);
	*stats = mstats;
	m->gcing = 0;
	runtime_semrelease(&runtime_worldsema);
	runtime_starttheworld();
}

void runtime_debug_readGCStats(Slice*)
  __asm__("runtime_debug.readGCStats");

void
runtime_debug_readGCStats(Slice *pauses)
{
	uint64 *p;
	uint32 i, n;

	// Calling code in runtime/debug should make the slice large enough.
	if((size_t)pauses->cap < nelem(mstats.pause_ns)+3)
		runtime_throw("runtime: short slice passed to readGCStats");

	// Pass back: pauses, last gc (absolute time), number of gc, total pause ns.
	p = (uint64*)pauses->array;
	runtime_lock(runtime_mheap);
	n = mstats.numgc;
	if(n > nelem(mstats.pause_ns))
		n = nelem(mstats.pause_ns);
	
	// The pause buffer is circular. The most recent pause is at
	// pause_ns[(numgc-1)%nelem(pause_ns)], and then backward
	// from there to go back farther in time. We deliver the times
	// most recent first (in p[0]).
	for(i=0; i<n; i++)
		p[i] = mstats.pause_ns[(mstats.numgc-1-i)%nelem(mstats.pause_ns)];

	p[n] = mstats.last_gc;
	p[n+1] = mstats.numgc;
	p[n+2] = mstats.pause_total_ns;	
	runtime_unlock(runtime_mheap);
	pauses->__count = n+3;
}

intgo runtime_debug_setGCPercent(intgo)
  __asm__("runtime_debug.setGCPercent");

intgo
runtime_debug_setGCPercent(intgo in)
{
	intgo out;

	runtime_lock(runtime_mheap);
	if(gcpercent == GcpercentUnknown)
		gcpercent = readgogc();
	out = gcpercent;
	if(in < 0)
		in = -1;
	gcpercent = in;
	runtime_unlock(runtime_mheap);
	return out;
}

static void
gchelperstart(void)
{
	M *m;

	m = runtime_m();
	if(m->helpgc < 0 || m->helpgc >= MaxGcproc)
		runtime_throw("gchelperstart: bad m->helpgc");
	if(runtime_xchg(&bufferList[m->helpgc].busy, 1))
		runtime_throw("gchelperstart: already busy");
}

static void
runfinq(void* dummy __attribute__ ((unused)))
{
	Finalizer *f;
	FinBlock *fb, *next;
	uint32 i;

	for(;;) {
		// There's no need for a lock in this section
		// because it only conflicts with the garbage
		// collector, and the garbage collector only
		// runs when everyone else is stopped, and
		// runfinq only stops at the gosched() or
		// during the calls in the for loop.
		fb = finq;
		finq = nil;
		if(fb == nil) {
			fingwait = 1;
			runtime_park(nil, nil, "finalizer wait");
			continue;
		}
		if(raceenabled)
			runtime_racefingo();
		for(; fb; fb=next) {
			next = fb->next;
			for(i=0; i<(uint32)fb->cnt; i++) {
				void *param;

				f = &fb->fin[i];
				param = &f->arg;
				reflect_call(f->ft, f->fn, 0, 0, &param, nil);
				f->fn = nil;
				f->arg = nil;
			}
			fb->cnt = 0;
			fb->next = finc;
			finc = fb;
		}
		runtime_gc(1);	// trigger another gc to clean up the finalized objects, if possible
	}
}

// mark the block at v of size n as allocated.
// If noptr is true, mark it as having no pointers.
void
runtime_markallocated(void *v, uintptr n, bool noptr)
{
	uintptr *b, obits, bits, off, shift;

	if(0)
		runtime_printf("markallocated %p+%p\n", v, n);

	if((byte*)v+n > (byte*)runtime_mheap->arena_used || (byte*)v < runtime_mheap->arena_start)
		runtime_throw("markallocated: bad pointer");

	off = (uintptr*)v - (uintptr*)runtime_mheap->arena_start;  // word offset
	b = (uintptr*)runtime_mheap->arena_start - off/wordsPerBitmapWord - 1;
	shift = off % wordsPerBitmapWord;

	for(;;) {
		obits = *b;
		bits = (obits & ~(bitMask<<shift)) | (bitAllocated<<shift);
		if(noptr)
			bits |= bitNoPointers<<shift;
		if(runtime_singleproc) {
			*b = bits;
			break;
		} else {
			// more than one goroutine is potentially running: use atomic op
			if(runtime_casp((void**)b, (void*)obits, (void*)bits))
				break;
		}
	}
}

// mark the block at v of size n as freed.
void
runtime_markfreed(void *v, uintptr n)
{
	uintptr *b, obits, bits, off, shift;

	if(0)
		runtime_printf("markallocated %p+%p\n", v, n);

	if((byte*)v+n > (byte*)runtime_mheap->arena_used || (byte*)v < runtime_mheap->arena_start)
		runtime_throw("markallocated: bad pointer");

	off = (uintptr*)v - (uintptr*)runtime_mheap->arena_start;  // word offset
	b = (uintptr*)runtime_mheap->arena_start - off/wordsPerBitmapWord - 1;
	shift = off % wordsPerBitmapWord;

	for(;;) {
		obits = *b;
		bits = (obits & ~(bitMask<<shift)) | (bitBlockBoundary<<shift);
		if(runtime_singleproc) {
			*b = bits;
			break;
		} else {
			// more than one goroutine is potentially running: use atomic op
			if(runtime_casp((void**)b, (void*)obits, (void*)bits))
				break;
		}
	}
}

// check that the block at v of size n is marked freed.
void
runtime_checkfreed(void *v, uintptr n)
{
	uintptr *b, bits, off, shift;

	if(!runtime_checking)
		return;

	if((byte*)v+n > (byte*)runtime_mheap->arena_used || (byte*)v < runtime_mheap->arena_start)
		return;	// not allocated, so okay

	off = (uintptr*)v - (uintptr*)runtime_mheap->arena_start;  // word offset
	b = (uintptr*)runtime_mheap->arena_start - off/wordsPerBitmapWord - 1;
	shift = off % wordsPerBitmapWord;

	bits = *b>>shift;
	if((bits & bitAllocated) != 0) {
		runtime_printf("checkfreed %p+%p: off=%p have=%p\n",
			v, n, off, bits & bitMask);
		runtime_throw("checkfreed: not freed");
	}
}

// mark the span of memory at v as having n blocks of the given size.
// if leftover is true, there is left over space at the end of the span.
void
runtime_markspan(void *v, uintptr size, uintptr n, bool leftover)
{
	uintptr *b, off, shift;
	byte *p;

	if((byte*)v+size*n > (byte*)runtime_mheap->arena_used || (byte*)v < runtime_mheap->arena_start)
		runtime_throw("markspan: bad pointer");

	p = v;
	if(leftover)	// mark a boundary just past end of last block too
		n++;
	for(; n-- > 0; p += size) {
		// Okay to use non-atomic ops here, because we control
		// the entire span, and each bitmap word has bits for only
		// one span, so no other goroutines are changing these
		// bitmap words.
		off = (uintptr*)p - (uintptr*)runtime_mheap->arena_start;  // word offset
		b = (uintptr*)runtime_mheap->arena_start - off/wordsPerBitmapWord - 1;
		shift = off % wordsPerBitmapWord;
		*b = (*b & ~(bitMask<<shift)) | (bitBlockBoundary<<shift);
	}
}

// unmark the span of memory at v of length n bytes.
void
runtime_unmarkspan(void *v, uintptr n)
{
	uintptr *p, *b, off;

	if((byte*)v+n > (byte*)runtime_mheap->arena_used || (byte*)v < runtime_mheap->arena_start)
		runtime_throw("markspan: bad pointer");

	p = v;
	off = p - (uintptr*)runtime_mheap->arena_start;  // word offset
	if(off % wordsPerBitmapWord != 0)
		runtime_throw("markspan: unaligned pointer");
	b = (uintptr*)runtime_mheap->arena_start - off/wordsPerBitmapWord - 1;
	n /= PtrSize;
	if(n%wordsPerBitmapWord != 0)
		runtime_throw("unmarkspan: unaligned length");
	// Okay to use non-atomic ops here, because we control
	// the entire span, and each bitmap word has bits for only
	// one span, so no other goroutines are changing these
	// bitmap words.
	n /= wordsPerBitmapWord;
	while(n-- > 0)
		*b-- = 0;
}

bool
runtime_blockspecial(void *v)
{
	uintptr *b, off, shift;

	if(DebugMark)
		return true;

	off = (uintptr*)v - (uintptr*)runtime_mheap->arena_start;
	b = (uintptr*)runtime_mheap->arena_start - off/wordsPerBitmapWord - 1;
	shift = off % wordsPerBitmapWord;

	return (*b & (bitSpecial<<shift)) != 0;
}

void
runtime_setblockspecial(void *v, bool s)
{
	uintptr *b, off, shift, bits, obits;

	if(DebugMark)
		return;

	off = (uintptr*)v - (uintptr*)runtime_mheap->arena_start;
	b = (uintptr*)runtime_mheap->arena_start - off/wordsPerBitmapWord - 1;
	shift = off % wordsPerBitmapWord;

	for(;;) {
		obits = *b;
		if(s)
			bits = obits | (bitSpecial<<shift);
		else
			bits = obits & ~(bitSpecial<<shift);
		if(runtime_singleproc) {
			*b = bits;
			break;
		} else {
			// more than one goroutine is potentially running: use atomic op
			if(runtime_casp((void**)b, (void*)obits, (void*)bits))
				break;
		}
	}
}

void
runtime_MHeap_MapBits(MHeap *h)
{
	size_t page_size;

	// Caller has added extra mappings to the arena.
	// Add extra mappings of bitmap words as needed.
	// We allocate extra bitmap pieces in chunks of bitmapChunk.
	enum {
		bitmapChunk = 8192
	};
	uintptr n;

	n = (h->arena_used - h->arena_start) / wordsPerBitmapWord;
	n = (n+bitmapChunk-1) & ~(bitmapChunk-1);
	if(h->bitmap_mapped >= n)
		return;

	page_size = getpagesize();
	n = (n+page_size-1) & ~(page_size-1);

	runtime_SysMap(h->arena_start - n, n - h->bitmap_mapped);
	h->bitmap_mapped = n;
}
