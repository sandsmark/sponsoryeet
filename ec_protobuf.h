/*!
\file ec_protobuf.h
\author	jiangyong
\email  kipway@outlook.com
\update 2021.5.6

classes to encode/decode google protocol buffer,support proto3

eclib 3.0 Copyright (c) 2017-2021, kipway
source repository : https://github.com/kipway

Licensed under the Apache License, Version 2.0 (the "License");
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
*/

#pragma once

#include <stdint.h>
#include <memory.h>
#include <string>
#include <vector>
#ifdef _MSC_VER
#ifndef bswap_16
#	define bswap_16(x) _byteswap_ushort(x)
#endif
#ifndef bswap_32
#	define bswap_32(x) _byteswap_ulong(x)
#endif
#ifndef bswap_64
#	define bswap_64(x) _byteswap_uint64(x)
#endif
#else
#include <byteswap.h>
#endif
#include <type_traits>
#define pb_varint  0  // Varint
#define pb_fixed64 1  // 64-bit
#define pb_length_delimited  2  // Length-delimited
#define pb_start_group  3 // deprecated not support
#define pb_end_group  4   //deprecated not support
#define pb_fixed32 5  // 32-bit

#define pberr_sucess 0
#define pberr_ok 0
#define pberr_encode 1
#define pberr_decode 2
#define pberr_str_non_utf8 3
#define pberr_memory  4

namespace ec
{
	/*!
	\brief encode and decode
	see https://developers.google.com/protocol-buffers/docs/encoding
	*/
	class base_protobuf //base class for encode and decode protobuf
	{
	protected:
		int _lasterr;
	public:
		inline bool isbig() const
		{
			union {
				uint32_t u32;
				uint8_t u8;
			} ua;
			ua.u32 = 0x01020304;
			return ua.u8 == 0x01;
		}

		inline int getlasterr()
		{
			return _lasterr;
		}

		bool isutf8(const char* s, size_t size = 0) const // return true if s is utf8
		{
			uint8_t c;
			int nb = 0;
			const char* pend = s + (size ? size : strlen(s));
			while (s < pend) {
				c = *s++;
				if (!nb) {
					if (!(c & 0x80))
						continue;
					if (c >= 0xFC && c <= 0xFD)
						nb = 5;
					else if (c >= 0xF8)
						nb = 4;
					else if (c >= 0xF0)
						nb = 3;
					else if (c >= 0xE0)
						nb = 2;
					//else if (c >= 0xC0) // GBK -> UTF8  > 2bytes
					//	nb = 1;
					else
						return false;
					continue;
				}
				if ((c & 0xC0) != 0x80)
					return false;
				nb--;
			}
			return !nb;
		}

		base_protobuf() : _lasterr(0)
		{
		}

		template<class _Tp
			, class = typename std::enable_if<std::is_integral<_Tp>::value>::type >
			struct t_zigzag {
			using utype = typename std::conditional < sizeof(_Tp) < 8u, uint32_t, uint64_t > ::type;
			using itype = typename std::conditional < sizeof(_Tp) < 8u, int32_t, int64_t > ::type;
			inline utype  encode(itype v) const {
				return ((v << 1) ^ (v >> (sizeof(_Tp) < 8u ? 31 : 63)));
			}

			inline  itype decode(utype v) const {
				return (itype)((v >> 1) ^ (-(itype)(v & 1)));
			}
		};

	protected:
		template<class _Tp
			, class = typename std::enable_if<std::is_integral<_Tp>::value && std::is_unsigned<_Tp>::value> ::type >
			bool get_varint(const uint8_t* &pd, int &len, _Tp &out)  //get Varint (Base 128 Varints)
		{
			if (len <= 0)
				return false;
			int nbit = 0;
			out = 0;
			do {
				out |= (*pd & (_Tp)0x7F) << nbit;
				if (!(*pd & 0x80)) {
					pd++;
					len--;
					return true;
				}
				nbit += 7;
				pd++;
				len--;
			} while (len > 0 && nbit < ((int)sizeof(_Tp) * 8));
			_lasterr = pberr_decode;
			return false;
		}

		template<class _Tp
			, class _Out
			, class = typename std::enable_if<std::is_integral<_Tp>::value && std::is_unsigned<_Tp>::value > ::type >
			bool out_varint(_Tp v, _Out* pout) //out Varint (Base 128 Varints)
		{
			int nbit = 0;
			uint8_t out = 0;
			do {
				out = (v >> nbit) & 0x7F;
				nbit += 7;
				if (v >> nbit) {
					out |= 0x80;
					pout->push_back(out);
				}
				else {
					pout->push_back(out);
					break;
				}
			} while (nbit + 7 < ((int)sizeof(_Tp) * 8));
			if (v >> nbit) {
				try {
					pout->push_back((uint8_t)(v >> nbit));
				}
				catch (...) {
					_lasterr = pberr_memory;
					return false;
				}
			}
			return true;
		}

	public:
		template<class _Tp
			, class = typename std::enable_if<std::is_arithmetic<_Tp>::value && (sizeof(_Tp) == 4u || sizeof(_Tp) == 8u)>::type >
			bool get_fixed(const uint8_t* &pd, int &len, _Tp &out) const  //get 32-bit and 64-bit (fixed32,sfixed32,fixed64,sfixed64,float,double)
		{
			if (len < (int)sizeof(out))
				return false;
			memcpy(&out, pd, sizeof(out));
			if (isbig()) {
				if (4 == sizeof(out))
					*((uint32_t*)&out) = bswap_32(*((uint32_t*)&out));
				else
					*((uint64_t*)&out) = bswap_64(*((uint64_t*)&out));
			}
			pd += sizeof(out);
			len -= sizeof(out);
			return true;
		}

		template<class _Tp
			, class _Out
			, class = typename std::enable_if<std::is_arithmetic<_Tp>::value && sizeof(_Tp) == 4u >::type>
			bool out_fixed32(_Tp v, _Out * pout)  //out 32-bit (fixed32,sfixed32,float)
		{
			if (isbig()) {
				uint32_t uv; memcpy(&uv, &v, 4); uv = bswap_32(uv);
				try {
					pout->append((uint8_t*)&uv, sizeof(uv));
					return true;
				}
				catch (...) {
					_lasterr = pberr_memory;
					return false;
				}
			}
			try {
				pout->append((uint8_t*)&v, sizeof(v));
				return true;
			}
			catch (...) {
				_lasterr = pberr_memory;
				return false;
			}
		}

		template<class _Tp
			, class _Out
			, class = typename std::enable_if<std::is_arithmetic<_Tp>::value && sizeof(_Tp) == 8u>::type>
			bool out_fixed64(_Tp v, _Out * pout)  //out 64-bit (fixed64,sfixed64,double)
		{
			if (isbig()) {
				uint64_t uv; memcpy(&uv, &v, 8); uv = bswap_64(uv);
				try {
					pout->append((uint8_t*)&uv, sizeof(uv));
					return true;
				}
				catch (...) {
					_lasterr = pberr_memory;
					return false;
				}
			}
			try {
				pout->append((uint8_t*)&v, sizeof(v));
				return true;
			}
			catch (...) {
				_lasterr = pberr_memory;
				return false;
			}
		}

		bool get_key(const uint8_t* &pd, int &len, uint32_t &field_number, uint32_t &wire_type) //get field_number and  wire_type
		{
			uint32_t key;
			if (!get_varint(pd, len, key))
				return false;
			wire_type = key & 0x07;
			field_number = key >> 3;
			return true;
		}

		bool get_length_delimited(const uint8_t* &pd, int &len, const uint8_t* &pout, size_t &outlen)  //get string, bytes,no copy
		{
			uint32_t ul = 0;
			if (!get_varint(pd, len, ul))
				return false;
			if (len < (int)ul) {
				_lasterr = pberr_decode;
				return false;
			}
			pout = pd;
			pd += ul;
			len -= ul;
			outlen = ul;
			return true;
		}

		bool jump_over(const uint8_t* &pd, int &len, uint32_t wire_type) //jump over unkown field_number
		{
			size_t datalen = 0;
			uint64_t v = 0;
			switch (wire_type) {
			case pb_varint:
				return get_varint(pd, len, v);
				break;
			case pb_fixed64:
				if (len < 8)
					return false;
				pd += 8;
				len -= 8;
				break;
			case pb_length_delimited:
				if (!get_varint(pd, len, datalen))
					return false;
				if (len < (int)datalen)
					return false;
				pd += datalen;
				len -= (int)datalen;
				break;
			case pb_fixed32:
				if (len < 4)
					return false;
				pd += 4;
				len -= 4;
				break;
			default:
				_lasterr = pberr_decode;
				return false;// unkown wire_type
			}
			return true;
		}

		template<class _Out>
		bool out_length_delimited(const uint8_t* pd, size_t len, _Out* pout) // out string, bytes
		{
			if (!out_varint(len, pout))
				return false;
			try {
				pout->append(pd, len);
				return true;
			}
			catch (...) {
				_lasterr = pberr_memory;
				return false;
			}
		}

		template<class _Out>
		bool out_key(uint32_t field_number, uint32_t wire_type, _Out* pout) // out field_number and  wire_type
		{
			uint32_t v = (field_number << 3) | (wire_type & 0x07);
			return out_varint(v, pout);
		}
	};

	class msg_protoc3 : public base_protobuf
	{
	public:
		msg_protoc3()
		{
		}

		bool p_bytes(uint32_t wire_type, const uint8_t* &pd, int &len, void* pout, size_t &outlen)
		{
			size_t tmpsize = 0;
			const uint8_t* ptmp = nullptr;
			if (pb_length_delimited != wire_type || !get_length_delimited(pd, len, ptmp, tmpsize))
				return false;
			memcpy(pout, ptmp, tmpsize <= outlen ? tmpsize : outlen);
			outlen = tmpsize;
			return tmpsize <= outlen;
		}

		bool p_str(uint32_t wire_type, const uint8_t* &pd, int &len, char* pout, size_t outlen)
		{
			size_t tmpsize = 0;
			const uint8_t* ptmp = nullptr;
			if (pb_length_delimited != wire_type || !get_length_delimited(pd, len, ptmp, tmpsize))
				return false;
			size_t zlen = tmpsize < outlen ? tmpsize : outlen - 1u;
			memcpy(pout, ptmp, zlen);
			pout[zlen] = 0;
			if (!isutf8(pout, zlen)) {
				_lasterr = pberr_str_non_utf8;
				return false;
			}
			return tmpsize < outlen;
		}

		inline bool p_cls(uint32_t wire_type, const uint8_t* &pd, int &len, const uint8_t* &p, size_t &size)  // no copy
		{
			return (pb_length_delimited == wire_type && get_length_delimited(pd, len, p, size));
		}

		template< class _Out>
		bool p_cls(uint32_t wire_type, const uint8_t* &pd, int &len, _Out* pout)
		{
			size_t tmpsize = 0;
			const uint8_t* ptmp = nullptr;
			if (pb_length_delimited != wire_type || !get_length_delimited(pd, len, ptmp, tmpsize))
				return false;
			try {
				pout->append(ptmp, tmpsize);
				return true;
			}
			catch (...) {
				_lasterr = pberr_memory;
				return false;
			}
		}

		template<class _Tp
			, class = typename std::enable_if<std::is_integral<_Tp>::value>::type>
			bool p_var(uint32_t wire_type, const uint8_t* &pd, int &len, _Tp &out, bool zigzag = false)
		{
			if (pb_varint != wire_type)
				return false;
			typename std::conditional < sizeof(_Tp) < 8u, uint32_t, uint64_t > ::type v;
			if (!get_varint(pd, len, v))
				return false;
			if (zigzag)
				out = (_Tp)t_zigzag<_Tp>().decode(v);
			else
				out = (_Tp)v;
			return true;
		}

		template<class _Tp>
		inline bool p_fixed32(uint32_t wire_type, const uint8_t* &pd, int &len, _Tp &out)
		{
			return pb_fixed32 == wire_type && 4u == sizeof(out) && get_fixed(pd, len, out);
		}

		template<class _Tp>
		inline bool p_fixed64(uint32_t wire_type, const uint8_t* &pd, int &len, _Tp &out)
		{
			return pb_fixed64 == wire_type && 8u == sizeof(out) && get_fixed(pd, len, out);
		}

		template<class _Tp
			, class _Out
			, class = typename std::enable_if<std::is_integral<_Tp>::value>::type>
			bool out_var(_Out* po, int id, _Tp v, bool zigzag = false)
		{
			if (!v) //default 0
				return true;
			if (zigzag)
				return out_key(id, pb_varint, po) && out_varint(t_zigzag<_Tp>().encode(v), po);
			using utype = typename std::conditional < sizeof(_Tp) < 8u, uint32_t, uint64_t > ::type;
			return out_key(id, pb_varint, po) && out_varint((utype)v, po);
		}

		template<class _Out>
		bool out_str(_Out* po, int id, const char* s)
		{
			if (!s || !*s)
				return true;
			if (!isutf8(s)) {
				_lasterr = pberr_str_non_utf8;
				return false;
			}
			return out_key(id, pb_length_delimited, po) && out_length_delimited((uint8_t*)s, strlen(s), po);
		}

		template<class _Out>
		bool out_str(_Out* po, int id, const char* s, size_t size)
		{
			if (!s || !*s || !size)
				return true;
			if (!isutf8(s, size)) {
				_lasterr = pberr_str_non_utf8;
				return false;
			}
			return out_key(id, pb_length_delimited, po) && out_length_delimited((uint8_t*)s, size, po);
		}

		template<class _Out>
		bool out_cls(_Out* po, int id, const void* pcls, size_t size)
		{
			if (!pcls || !size)
				return true;
			return out_key(id, pb_length_delimited, po) && out_length_delimited((uint8_t*)pcls, size, po);
		}

		template<class _Out>
		bool out_cls_head(_Out* po, int id, size_t size)
		{
			if (!size)
				return true;
			return out_key(id, pb_length_delimited, po) && out_varint(size, po);
		}

		template<class _Tp
			, class _Out
			, class = typename std::enable_if<std::is_arithmetic<_Tp>::value && sizeof(_Tp) == 4u>::type >
			inline bool out_fixed32(_Out* po, int id, _Tp v)
		{
			return out_key(id, pb_fixed32, po) && base_protobuf::out_fixed32(v, po);
		}

		template<class _Tp
			, class _Out
			, class = typename std::enable_if<std::is_arithmetic<_Tp>::value && sizeof(_Tp) == 8u>::type >
			inline bool out_fixed64(_Out* po, int id, _Tp v)
		{
			return out_key(id, pb_fixed64, po) && base_protobuf::out_fixed64(v, po);
		}

	public: //sizeof
		template<class _Tp, class = typename std::enable_if<std::is_integral<_Tp>::value && std::is_unsigned<_Tp>::value > ::type >
		size_t size_varint(_Tp v) const //size Varint (Base 128 Varints)
		{
			int nbit = 0;
			size_t zret = 0;
			do {
				nbit += 7;
				zret++;
				if (!(v >> nbit))
					break;
			} while (nbit + 7 < ((int)sizeof(_Tp) * 8));
			if (v >> nbit)
				zret++;
			return zret;
		}

		inline size_t size_key(uint32_t field_number, uint32_t wire_type) const
		{
			return size_varint((field_number << 3) | (wire_type & 0x07));
		}

		inline size_t size_length_delimited(size_t len) const // size string, bytes
		{
			return len ? size_varint(len) + len : 0;
		}

		template<class _Tp
			, class = typename std::enable_if<std::is_integral<_Tp>::value>::type >
			size_t size_var(int id, _Tp v, bool zigzag = false) const
		{
			using utype = typename std::conditional < sizeof(_Tp) < 8u, uint32_t, uint64_t > ::type;
			if (!v)
				return 0; //default 0
			if (zigzag)
				return size_key(id, pb_varint) + size_varint(t_zigzag<_Tp>().encode(v));
			return size_key(id, pb_varint) + size_varint((utype)v);
		}

		template<class _Tp
			, class = typename std::enable_if<std::is_arithmetic<_Tp>::value && (sizeof(_Tp) == 4u || sizeof(_Tp) == 8u)>::type >
			size_t size_fixed(uint32_t id, _Tp v) const
		{
			if (4 == (int)sizeof(v))
				return size_key(id, 5) + 4;
			if (8 == (int)sizeof(v))
				return size_key(id, 1) + 8;
			return 0;
		}

		inline size_t size_str(uint32_t id, const char* s) const
		{
			if (!s || !*s)
				return 0;
			return size_key(id, pb_length_delimited) + size_length_delimited(strlen(s));
		}

		inline size_t size_str(uint32_t id, const char* s, size_t size) const
		{
			if (!s || !*s || !size)
				return 0;
			return size_key(id, pb_length_delimited) + size_length_delimited(size);
		}

		inline size_t size_cls(uint32_t id, const void* pcls, size_t size) const
		{
			if (!pcls || !size)
				return 0;
			return size_key(id, pb_length_delimited) + size_length_delimited(size);
		}

		inline size_t size_cls(uint32_t id, size_t size) const
		{
			if (!size)
				return 0;
			return size_key(id, pb_length_delimited) + size_length_delimited(size);
		}
	};

	template<class _Out = std::basic_string<uint8_t>>
	class cls_protoc3 : public msg_protoc3
	{
	public:
		cls_protoc3() {
		}
		virtual ~cls_protoc3() {
		}
	public:
		static size_t cpstr(char* sout, size_t sizeout, const void* psrc, size_t sizesrc)
		{
			if (!psrc || !sizesrc) {
				*sout = 0;
				return 0;
			}
			size_t zc = sizeout > sizesrc ? sizesrc : sizeout - 1;
			memcpy(sout, psrc, zc);
			sout[zc] = 0;
			return zc;
		}

		const char* getlasterrstr()
		{
			const char* sret = nullptr;
			switch (_lasterr) {
			case pberr_ok:
				sret = "";
				break;
			case pberr_encode:
				sret = "encode failed";
				break;

			case pberr_decode:
				sret = "decode failed";
				break;
			case pberr_str_non_utf8:
				sret = "string is not utf8 encode";
				break;
			case pberr_memory:
				sret = "memory error";
				break;
			}
			return sret;
		}

		size_t size(uint32_t uid)
		{
			size_t sizebody = size_content();
			if (!sizebody)
				return 0;
			return size_key(uid, pb_length_delimited) + size_length_delimited(sizebody);
		}

		bool serialize(uint32_t id, _Out* pout)
		{
			_lasterr = pberr_ok;
			size_t sizebody = size_content();
			if (!sizebody)
				return true;
			return out_key(id, pb_length_delimited, pout) && out_varint(sizebody, pout)
				&& out_content(pout);
		}

		inline size_t size()
		{
			return size_content();
		}

		template<class _OBJ>
		size_t size_obj_array(int objid, std::vector<_OBJ> &vs)
		{
			size_t zu = 0;
			for (auto &i : vs) {
				zu += i.size(objid);
			}
			return zu;
		}

		template<class _OBJ>
		bool out_obj_array(_Out * pout, int objid, std::vector<_OBJ> &vs)
		{
			for (auto &i : vs) {
				if (!i.serialize(objid, pout))
					return false;
			}
			return true;
		}

		template<class _OBJ>
		bool parse_obj_array(const void* pdata, size_t sizedata, std::vector<_OBJ> &vs) {
			_OBJ t;
			bool bret = t.parse(pdata, sizedata);
			if (bret)
				vs.push_back(std::move(t));
			return bret;
		}

		inline bool serialize(_Out* pout)
		{
			_lasterr = pberr_ok;
			return out_content(pout);
		}

		bool parse(const void* pmsg, size_t msgsize) // pasre content
		{
			_lasterr = pberr_ok;
			reset();
			if (!pmsg || !msgsize)
				return true;
			uint32_t field_number = 0, wire_type = 0;
			int len = (int)msgsize;
			const uint8_t* pd = (const uint8_t*)pmsg;
			uint32_t u32 = 0;
			uint64_t u64 = 0;
			const uint8_t *pcls = nullptr;
			size_t  zlen = 0;
			do {
				if (!get_key(pd, len, field_number, wire_type))
					return false;
				switch (wire_type) {
				case pb_varint:
					if (!get_varint(pd, len, u64) || !on_var(field_number, u64))
						return false;
					break;
				case pb_length_delimited:
					if (!p_cls(wire_type, pd, len, pcls, zlen) || !on_cls(field_number, pcls, zlen))
						return false;
					break;
				case pb_fixed32:
					if (!p_fixed32(wire_type, pd, len, u32) || !on_fix32(field_number, &u32))
						return false;
					break;
				case pb_fixed64:
					if (!p_fixed64(wire_type, pd, len, u64) || !on_fix64(field_number, &u64))
						return false;
					break;
				default:
					if (!jump_over(pd, len, wire_type))
						return false;
					break;
				}
			} while (len > 0);
			return 0 == len;
		}

		template<class _Tp
			, class = typename std::enable_if<std::is_integral<_Tp>::value>::type >
			bool p_varpacket(const uint8_t* &pd, int &len, _Tp &out, bool zigzag = false)
		{
			if (!std::is_arithmetic<_Tp>::value)
				return false;
			using utype = typename std::conditional < sizeof(_Tp) < 8u, uint32_t, uint64_t > ::type;
			utype v;
			if (!get_varint(pd, len, v))
				return false;
			if (zigzag)
				out = (_Tp)t_zigzag<_Tp>().decode(v);
			else
				out = (_Tp)v;
			return true;
		}

		template<class _Tp
			, class = typename std::enable_if<std::is_integral<_Tp>::value>::type >
			bool out_varpacket(uint32_t id, _Tp *pdata, size_t items, _Out* pout, bool zigzag = false)
		{
			using utype = typename std::conditional < sizeof(_Tp) < 8u, uint32_t, uint64_t > ::type;
			if (!items)
				return true;
			size_t zbody = size_varpacket(pdata, items, zigzag);
			out_key(id, pb_length_delimited, pout);
			out_varint(zbody, pout);
			for (auto i = 0u; i < items; i++) {
				if (zigzag) {
					t_zigzag<_Tp> zg;
					if (!out_varint(zg.encode(pdata[i]), pout))
						return false;
				}
				else {
					if (!out_varint((utype)pdata[i], pout))
						return false;
				}
			}
			return true;
		}

		template<class _Tp
			, class = typename std::enable_if<std::is_integral<_Tp>::value>::type >
			size_t size_varpacket(_Tp *pdata, size_t items, bool zigzag = false)
		{
			if (!items)
				return 0;
			size_t zn = 0;
			for (auto i = 0u; i < items; i++) {
				if (zigzag)
					zn += size_varint(t_zigzag<_Tp>().encode(pdata[i]));
				else {
					using utype = typename std::conditional < sizeof(_Tp) < 8u, uint32_t, uint64_t > ::type;
					zn += size_varint((utype)pdata[i]);
				}
			}
			return zn;
		}

		template<class _Tp
			, class = typename std::enable_if<std::is_integral<_Tp>::value>::type >
			size_t size_varpacket(uint32_t id, _Tp *pdata, size_t items, bool zigzag = false)
		{
			if (!items)
				return 0;
			size_t zn = 0;
			for (auto i = 0u; i < items; i++) {
				if (zigzag)
					zn += size_varint(t_zigzag<_Tp>().encode(pdata[i]));
				else {
					using utype = typename std::conditional < sizeof(_Tp) < 8u, uint32_t, uint64_t > ::type;
					zn += size_varint((utype)pdata[i]);
				}
			}
			return size_key(id, pb_length_delimited) + size_varint(zn) + zn;
		}

		template<class _Tp,
			class = typename std::enable_if<std::is_arithmetic<_Tp>::value && sizeof(_Tp) == 4u>::type >
			size_t size_fix32packet(uint32_t id, _Tp *pdata, size_t items)
		{
			if (!items)
				return 0;
			return size_key(id, pb_length_delimited) + size_varint(sizeof(_Tp) * items) + sizeof(_Tp) * items;
		}

		template<class _Tp,
			class = typename std::enable_if<std::is_arithmetic<_Tp>::value && sizeof(_Tp) == 4u>::type>
			bool out_fix32packet(uint32_t id, _Tp *pdata, size_t items, _Out* pout)
		{
			if (!items)
				return true;
			size_t zbody = sizeof(_Tp) * items;
			out_key(id, pb_length_delimited, pout);
			out_varint(zbody, pout);
			for (auto i = 0u; i < items; i++)
				base_protobuf::out_fixed32(pdata[i], pout);
			return true;
		}

		template<class _Tp,
			class = typename std::enable_if<std::is_arithmetic<_Tp>::value && sizeof(_Tp) == 8u>::type >
			size_t size_fix64packet(uint32_t id, _Tp *pdata, size_t items)
		{
			if (!items)
				return 0;
			return size_key(id, pb_length_delimited) + size_varint(sizeof(_Tp) * items) + sizeof(_Tp) * items;
		}

		template<class _Tp,
			class = typename std::enable_if<std::is_arithmetic<_Tp>::value && sizeof(_Tp) == 8u>::type>
			bool out_fix64packet(uint32_t id, _Tp *pdata, size_t items, _Out* pout)
		{
			if (!items)
				return true;
			size_t zbody = sizeof(_Tp) * items;
			out_key(id, pb_length_delimited, pout);
			out_varint(zbody, pout);
			for (auto i = 0u; i < items; i++)
				base_protobuf::out_fixed64(pdata[i], pout);
			return true;
		}

		template<class _Str = std::string>
		bool cls2str(const void *pd, size_t size, _Str &s)
		{
			if (!isutf8((const char*)pd, size)) {
				_lasterr = pberr_str_non_utf8;
				return false;
			}
			try {
				s.assign((const char*)pd, size);
			}
			catch (...) {
				_lasterr = pberr_memory;
				return false;
			}
			return true;
		}
	public:
		virtual void reset() = 0;
	protected:
		virtual size_t size_content() = 0;
		virtual bool out_content(_Out* pout) = 0;
		/*
		For historical reasons, repeated fields of scalar numeric types aren't encoded as efficiently as they could be.
		New code should use the special option [packed=true] to get a more efficient encoding. For example:
			repeated int32 samples = 4 [packed = true];

		A packed repeated field containing zero elements does not appear in the encoded message.
		Otherwise, all of the elements of the field are packed into a single key-value pair with
		wire type 2 (length-delimited). Each element is encoded the same way it would be normally,
		except without a key preceding it.

		For example, imagine you have the message type:

		message Test4 {
			repeated int32 d = 4 [packed=true];
		}

		Now let's say you construct a Test4, providing the values 3, 270, and 86942 for the repeated field d.
		Then, the encoded form would be:

		22        // key (field number 4, wire type 2)
		06        // payload size (6 bytes)
		03        // first element (varint 3)
		8E 02     // second element (varint 270)
		9E A7 05  // third element (varint 86942)

		*/

		virtual bool on_cls(uint32_t field_number, const void* pdata, size_t sizedata) {
			return true;
		}
		virtual bool on_var(uint32_t field_number, uint64_t val) {
			return true;
		}
		virtual bool on_fix32(uint32_t field_number, const void* pval) {
			return true;
		}
		virtual bool on_fix64(uint32_t field_number, const void* pval) {
			return true;
		}
	};

#define P3_FIX32(FID,VAR) case FID: \
	if (!p_fixed32(wire_type, pd, len, VAR)) \
		return false; \
	break

#define P3_FIX64(FID,VAR) case FID: \
	if (!p_fixed64(wire_type, pd, len, VAR)) \
		return false; \
	break

#define P3_STR(FID,STR) case FID: \
	if (!p_str(wire_type, pd, len, STR, sizeof(STR))) \
		return false; \
	break

#define P3_VAR(FID,VAR) case FID: \
	if (!p_var(wire_type, pd, len, VAR, false)) \
		return false; \
	break

#define P3_VAR_ZIG(FID,VAR) case FID: \
	if (!p_var(wire_type, pd, len, VAR, true)) \
		return false; \
	break

#define P3_BYTES(FID,OUT,REFOUTSIZE) case FID: \
	if (!p_bytes(wire_type, pd, len, OUT, REFOUTSIZE)) \
		return false; \
	break

#define P3_CLS_NOCPY(FID,PPD,PDSIZE) case FID: \
	if (!p_cls(wire_type, pd, len, PPD, PDSIZE)) \
		return false; \
	break

#define P3_DEFAULT default:\
	if (!jump_over(pd, len, wire_type))\
		return false;\
	break
}//ec
