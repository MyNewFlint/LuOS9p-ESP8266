/*
 * Copyright (c) 2013, 2014, Lourival Vieira Neto <lneto@NetBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the Author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef _KERNEL
#include <string.h>
#else
#include <lib/libkern/libkern.h>
#endif

#include <sys/param.h>

#include <lauxlib.h>

#include "luautil.h"

#include "data.h"
#include "binary.h"
#include "layout.h"

#define LUA_INTEGER_BYTE	(sizeof(lua_Integer))
#define LUA_INTEGER_BIT		(LUA_INTEGER_BYTE * BYTE_BIT)

inline static bool
check_offset(data_t *data, size_t offset, size_t length)
{
	size_t last_pos = data->offset + data->length - 1;
	return offset >= data->offset && offset <= last_pos;
}

inline static bool
check_length(data_t *data, size_t offset, size_t length)
{
	return offset + length <= data->offset + data->length;
}

inline static bool
check_limits(data_t *data, size_t offset, size_t length)
{
	return check_offset(data, offset, length) &&
		check_length(data, offset, length);
}

#define ENTRY_BYTE_OFFSET(data, entry) \
	((BIT_TO_BYTE(entry->offset + 1) - 1) + data->offset)

inline static bool
check_num_limits(data_t *data, layout_entry_t *entry)
{
	size_t offset = ENTRY_BYTE_OFFSET(data, entry);
	size_t length = BIT_TO_BYTE(entry->length);

	return entry->length <= LUA_INTEGER_BIT &&
		check_limits(data, offset, length);
}

inline static bool
check_str_limits(data_t *data, layout_entry_t *entry)
{
	size_t offset = entry->offset + data->offset;
	size_t length = entry->length;
	return check_limits(data, offset, length);
}

inline static layout_entry_t *
get_entry(lua_State *L, data_t *data, int key_ix)
{
	if (!luau_isvalidref(data->layout))
		return NULL;

	return layout_get_entry(L, data->layout, key_ix);
}

inline static bool
check_raw_ptr(data_t *data)
{
	return data->raw->ptr != NULL;
}

static data_raw_t *
new_raw(lua_State *L, void *ptr, size_t size, bool free)
{
	data_raw_t *raw = (data_raw_t *) luau_malloc(L, sizeof(data_raw_t));

	raw->ptr      = ptr;
	raw->size     = size;
	raw->refcount = 0;
	raw->free     = free;
	return raw;
}

static data_t *
new_data(lua_State *L, data_raw_t *raw, size_t offset, size_t length)
{
	data_t *data = lua_newuserdata(L, sizeof(data_t));

	data->raw    = raw;
	data->offset = offset;
	data->length = length;
	data->layout = LUA_REFNIL;

	luaL_getmetatable(L, DATA_USERDATA);
	lua_setmetatable(L, -2);

	return data;
}

#define ENTRY_BIT_OFFSET(data, entry) \
	(BYTE_TO_BIT(data->offset) + entry->offset)

#define BINARY_PARMS(data, entry) \
	data->raw->ptr, ENTRY_BIT_OFFSET(data, entry), \
	entry->length, entry->endian

static int
get_num(lua_State *L, data_t *data, layout_entry_t *entry)
{
	if (!check_num_limits(data, entry))
		return 0;

	/* assertion: LUA_INTEGER_BIT <= 64 */
	lua_Integer value = binary_get_uint64(BINARY_PARMS(data, entry));
	lua_pushinteger(L, value);
	return 1;
}

inline static int
get_str(lua_State *L, data_t *data, layout_entry_t *entry)
{
	if (!check_str_limits(data, entry))
		return 0;

	const char *ptr = (const char *) data_get_ptr(data); 
	const char *s = ptr + entry->offset;
	lua_pushlstring(L, s, entry->length);
	return 1;
}

inline static void
set_num(lua_State *L, data_t *data, layout_entry_t *entry, int value_ix)
{
	if (!check_num_limits(data, entry))
		return;

	lua_Integer value = lua_tointeger(L, value_ix);
	binary_set_uint64(BINARY_PARMS(data, entry), value);
}

static void
set_str(lua_State *L, data_t *data, layout_entry_t *entry, int value_ix)
{
	if (!check_str_limits(data, entry))
		return;

	size_t len;
	const char *s = lua_tolstring(L, value_ix, &len);
	if (s == NULL)
		return;

	char *ptr = (char *) data_get_ptr(data); 
	memcpy(ptr + entry->offset, s, MIN(len, entry->length));
}

inline data_t *
data_new(lua_State *L, void *ptr, size_t size, bool free)
{
	data_raw_t *raw  = new_raw(L, ptr, size, free);
	data_t     *data = new_data(L, raw, 0, size);
	return data;
}

int
data_new_segment(lua_State *L, data_t *data, size_t offset, size_t length)
{
	if (!check_limits(data, offset, length))
		return 0;

	new_data(L, data->raw, offset, length);

	data->raw->refcount++;
	return 1;
}

void
data_delete(lua_State *L, data_t *data)
{
	data_raw_t *raw = data->raw;

	if (raw->refcount == 0) {
		if (raw->free)
			luau_free(L, raw->ptr, raw->size);

		luau_free(L, raw, sizeof(data_raw_t));
	}
	else
		raw->refcount--;

	if (luau_isvalidref(data->layout))
		luau_unref(L, data->layout);
}

inline data_t *
data_test(lua_State *L, int index)
{
#if LUA_VERSION_NUM >= 502
	return (data_t *) luaL_testudata(L, index, DATA_USERDATA);
#else
	return (data_t *) luaL_checkudata(L, index, DATA_USERDATA);
#endif
}

inline void
data_apply_layout(lua_State *L, data_t *data, int layout_ix)
{
	luau_unref(L, data->layout);
	lua_pushvalue(L, layout_ix);
	data->layout = luau_ref(L);
}

int
data_get_field(lua_State *L, data_t *data, int key_ix)
{
	if (!check_raw_ptr(data))
		return 0;

	layout_entry_t *entry = get_entry(L, data, key_ix);
	if (entry == NULL)
		return 0;

	switch (entry->type) {
	case LAYOUT_TNUMBER:
		return get_num(L, data, entry);
	case LAYOUT_TSTRING:
		return get_str(L, data, entry);
	}
	return 0; /* unreached */
}

void
data_set_field(lua_State *L, data_t *data, int key_ix, int value_ix)
{
	if (!check_raw_ptr(data))
		return;

	layout_entry_t *entry = get_entry(L, data, key_ix);
	if (entry == NULL)
		return;

	switch (entry->type) {
	case LAYOUT_TNUMBER:
		set_num(L, data, entry, value_ix);
		break;
	case LAYOUT_TSTRING:
		set_str(L, data, entry, value_ix);
		break;
	}
}

inline void *
data_get_ptr(data_t * data)
{
	return (void *) ((char *) data->raw->ptr + data->offset);
}
