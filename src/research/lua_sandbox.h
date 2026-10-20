#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace research {
// Single bounded bank; address-ordered free blocks coalesce on every release.
// No Lua allocation escapes into the process/system allocator.
class LuaArena {
    struct Block { size_t size; Block* next; };
    union Alignment { lua_Number number; lua_Integer integer; void* pointer; };
    static const size_t kAlign = alignof(Alignment);
    static const size_t kHeader = (sizeof(Block) + kAlign - 1) & ~(kAlign - 1);
public:
    LuaArena(void* memory, size_t bytes) : free_(nullptr), used_(0), peak_(0), capacity_(0) {
        if(!memory) return;
        uintptr_t start = (reinterpret_cast<uintptr_t>(memory) + kAlign - 1) & ~(uintptr_t(kAlign) - 1);
        size_t skipped = start - reinterpret_cast<uintptr_t>(memory);
        if(bytes <= skipped + kHeader) return;
        capacity_ = (bytes - skipped) & ~(kAlign - 1);
        free_ = reinterpret_cast<Block*>(start); free_->size = capacity_; free_->next = nullptr;
    }
    void* resize(void* pointer, size_t bytes) {
        if(!bytes) { if(pointer) release(block(pointer)); return nullptr; }
        if(bytes > capacity_ || bytes > SIZE_MAX - kHeader - kAlign) return nullptr;
        const size_t need = (bytes + kHeader + kAlign - 1) & ~(kAlign - 1);
        Block* old = pointer ? block(pointer) : nullptr;
        if(old && need <= old->size) {
            if(old->size - need >= kHeader + kAlign) {
                Block* tail = reinterpret_cast<Block*>(reinterpret_cast<uint8_t*>(old) + need);
                tail->size = old->size - need; old->size = need;
                used_ -= tail->size; insert(tail);
            }
            return pointer;
        }
        if(old) {
            Block** next = &free_;
            while(*next && reinterpret_cast<uintptr_t>(*next) < reinterpret_cast<uintptr_t>(old)) next = &(*next)->next;
            if(*next && reinterpret_cast<uint8_t*>(old) + old->size == reinterpret_cast<uint8_t*>(*next) &&
               old->size + (*next)->size >= need) {
                Block* adjacent = *next; *next = adjacent->next;
                const size_t oldSize = old->size, total = oldSize + adjacent->size;
                old->size = total - need >= kHeader + kAlign ? need : total;
                if(total != old->size) {
                    Block* tail = reinterpret_cast<Block*>(reinterpret_cast<uint8_t*>(old) + old->size);
                    tail->size = total - old->size; insert(tail);
                }
                used_ += old->size - oldSize; if(used_ > peak_) peak_ = used_;
                return pointer;
            }
        }
        Block** slot = &free_;
        while(*slot && (*slot)->size < need) slot = &(*slot)->next;
        if(!*slot) return nullptr;
        Block* selected = *slot; *slot = selected->next;
        const size_t total = selected->size;
        selected->size = total - need >= kHeader + kAlign ? need : total;
        if(total != selected->size) {
            Block* tail = reinterpret_cast<Block*>(reinterpret_cast<uint8_t*>(selected) + selected->size);
            tail->size = total - selected->size; insert(tail);
        }
        used_ += selected->size; if(used_ > peak_) peak_ = used_;
        void* result = reinterpret_cast<uint8_t*>(selected) + kHeader;
        if(old) {
            const size_t available = old->size - kHeader;
            memcpy(result, pointer, available < bytes ? available : bytes); release(old);
        }
        return result;
    }
    size_t used() const { return used_; }
    size_t peak() const { return peak_; }
    size_t capacity() const { return capacity_; }
    static void* allocate(void* context, void* pointer, size_t, size_t bytes) {
        return static_cast<LuaArena*>(context)->resize(pointer, bytes);
    }
private:
    static Block* block(void* pointer) { return reinterpret_cast<Block*>(static_cast<uint8_t*>(pointer) - kHeader); }
    void release(Block* item) { used_ -= item->size; insert(item); }
    void insert(Block* item) {
        Block** slot = &free_; Block* previous = nullptr;
        while(*slot && reinterpret_cast<uintptr_t>(*slot) < reinterpret_cast<uintptr_t>(item)) {
            previous = *slot; slot = &(*slot)->next;
        }
        item->next = *slot; *slot = item;
        if(item->next && reinterpret_cast<uint8_t*>(item) + item->size == reinterpret_cast<uint8_t*>(item->next)) {
            item->size += item->next->size; item->next = item->next->next;
        }
        if(previous && reinterpret_cast<uint8_t*>(previous) + previous->size == reinterpret_cast<uint8_t*>(item)) {
            previous->size += item->size; previous->next = item->next;
        }
    }
    Block* free_;
    size_t used_, peak_, capacity_;
};

class LuaSandbox {
public:
    typedef uint32_t (*Clock)(void*);
    static const size_t kHeapBytes = 48 * 1024;
    static const size_t kSourceBytes = 32 * 1024;
    static const uint32_t kInstructions = 100000;
    static const uint32_t kWallUs = 5000;
    LuaSandbox(void* bank, size_t bytes, Clock clock, void* clockContext = nullptr)
        : arena_(bank, bytes), state_(nullptr), clock_(clock), clockContext_(clockContext),
          started_(0), operations_(0), argument_(0), callback_(nullptr), first_(nullptr), second_(nullptr), numeric_(false) {
        error_[0] = 0;
    }
    ~LuaSandbox() { close(); }
    bool open(lua_CFunction bindings = nullptr) {
        state_ = lua_newstate(LuaArena::allocate, &arena_);
        if(!state_) { setError("Lua VM needs more memory"); return false; }
        *static_cast<LuaSandbox**>(lua_getextraspace(state_)) = this;
        lua_pushcfunction(state_, openLibraries);
        if(lua_pcall(state_, 0, 0, 0) != LUA_OK) return captureError();
        if(bindings) {
            lua_pushcfunction(state_, bindings);
            if(lua_pcall(state_, 0, 0, 0) != LUA_OK) return captureError();
        }
        return true;
    }
    void close() { if(state_) { lua_close(state_); state_ = nullptr; } }
    bool load(const char* source, size_t length, const char* name) {
        if(!state_ || !source || length > kSourceBytes) { setError("Script exceeds 32 KiB source limit"); return false; }
        if(luaL_loadbufferx(state_, source, length, name, "t") != LUA_OK) return captureError();
        return protectedCall();
    }
    bool draw() { return invoke("draw", nullptr, nullptr, false, 0); }
    bool tick(uint32_t milliseconds) { return invoke("tick", nullptr, nullptr, true, milliseconds); }
    bool input(const char* button, const char* event) { return invoke("input", button, event, false, 0); }
    lua_State* state() const { return state_; }
    const char* error() const { return error_; }
    size_t used() const { return arena_.used(); }
    size_t peak() const { return arena_.peak(); }
    uint32_t instructions() const { return operations_; }
private:
    static LuaSandbox* owner(lua_State* state) { return *static_cast<LuaSandbox**>(lua_getextraspace(state)); }
    static void removeGlobal(lua_State* state, const char* name) { lua_pushnil(state); lua_setglobal(state, name); }
    static int openLibraries(lua_State* state) {
        luaL_requiref(state, LUA_GNAME, luaopen_base, 1); lua_pop(state, 1);
        luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(state, 1);
        luaL_requiref(state, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(state, 1);
        luaL_requiref(state, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(state, 1);
        const char* removed[] = {"dofile", "loadfile", "load", "pcall", "xpcall", "collectgarbage", "print", "warn",
                                 "getmetatable", "setmetatable"};
        for(size_t i = 0; i < sizeof(removed) / sizeof(removed[0]); ++i) removeGlobal(state, removed[i]);
        lua_getglobal(state, "string");
        const char* strings[] = {"dump", "rep", "find", "match", "gmatch", "gsub"};
        for(size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); ++i) { lua_pushnil(state); lua_setfield(state, -2, strings[i]); }
        lua_pop(state, 1);
        return 0;
    }
    static void hook(lua_State* state, lua_Debug*) {
        LuaSandbox* self = owner(state); self->operations_ += 500;
        if(self->operations_ >= kInstructions || uint32_t(self->now() - self->started_) >= kWallUs)
            luaL_error(state, "execution quota exceeded");
    }
    uint32_t now() const { return clock_ ? clock_(clockContext_) : 0; }
    bool protectedCall() {
        started_ = now(); operations_ = 0;
        lua_sethook(state_, hook, LUA_MASKCOUNT, 500);
        const int result = lua_pcall(state_, 0, 0, 0);
        lua_sethook(state_, nullptr, 0, 0);
        if(result != LUA_OK) return captureError();
        if(uint32_t(now() - started_) >= kWallUs) { setError("callback exceeded wall time quota"); return false; }
        return true;
    }
    bool captureError() {
        const char* message = lua_tostring(state_, -1);
        setError(message ? message : "Lua failed without an error string"); lua_pop(state_, 1); return false;
    }
    void setError(const char* text) { snprintf(error_, sizeof(error_), "%.127s", text); }
    static int dispatch(lua_State* state) {
        LuaSandbox* self = owner(state);
        lua_getglobal(state, "app");
        if(lua_isnil(state, -1)) return 0;
        if(!lua_istable(state, -1)) return luaL_error(state, "app must be a table");
        lua_getfield(state, -1, self->callback_);
        if(lua_isnil(state, -1)) return 0;
        if(!lua_isfunction(state, -1)) return luaL_error(state, "app callback must be a function");
        lua_remove(state, -2);
        int arguments = 0;
        if(self->numeric_) { lua_pushinteger(state, self->argument_); arguments = 1; }
        else if(self->first_) {
            lua_pushstring(state, self->first_); lua_pushstring(state, self->second_); arguments = 2;
        }
        lua_call(state, arguments, 0); return 0;
    }
    bool invoke(const char* callback, const char* first, const char* second, bool numeric, lua_Integer argument) {
        if(!state_) return false;
        callback_ = callback; first_ = first; second_ = second; numeric_ = numeric; argument_ = argument;
        lua_pushcfunction(state_, dispatch); return protectedCall();
    }
    LuaArena arena_;
    lua_State* state_;
    Clock clock_;
    void* clockContext_;
    uint32_t started_, operations_;
    lua_Integer argument_;
    const char* callback_;
    const char* first_;
    const char* second_;
    bool numeric_;
    char error_[128];
};
}
