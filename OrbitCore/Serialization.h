// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "PrintVar.h"
#include "Utils.h"

#if (__GNUC__ >= 7)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif

#if (__GNUC__ >= 8)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif

#include <cereal/access.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/archives/portable_binary.hpp>
#include <cereal/archives/xml.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/base_class.hpp>
#include <cereal/types/bitset.hpp>
#include <cereal/types/chrono.hpp>
#include <cereal/types/common.hpp>
#include <cereal/types/complex.hpp>
#include <cereal/types/deque.hpp>
#include <cereal/types/forward_list.hpp>
#include <cereal/types/list.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/polymorphic.hpp>
#include <cereal/types/queue.hpp>
#include <cereal/types/set.hpp>
#include <cereal/types/stack.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/tuple.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/unordered_set.hpp>
#include <cereal/types/utility.hpp>
#include <cereal/types/vector.hpp>

#include "SerializationMacros.h"

#if (__GNUC__ >= 8)
#pragma GCC diagnostic pop
#endif

#if (__GNUC__ >= 7)
#pragma GCC diagnostic pop
#endif

//-----------------------------------------------------------------------------
class CounterStreamBuffer : public std::streambuf {
 public:
  size_t Size() const { return m_Size; }
  void Reset() { m_Size = 0; }

 private:
  int_type overflow(int_type) override { return m_Size++; }
  int_type m_Size = 0;
};
extern CounterStreamBuffer GStreamCounter;

//-----------------------------------------------------------------------------
struct ScopeCounter {
  explicit ScopeCounter(const std::string& a_Msg) : m_Message(a_Msg) {
    m_SizeBegin = GStreamCounter.Size();
  }

  ~ScopeCounter() {
    m_SizeEnd = GStreamCounter.Size();
    std::string size = GetPrettySize(m_SizeEnd - m_SizeBegin);
    LOG("%s size: %s", m_Message.c_str(), size.c_str());
  }

  std::string m_Message;
  size_t m_SizeBegin;
  size_t m_SizeEnd;
};

//-----------------------------------------------------------------------------
template <typename T>
inline std::string SerializeObjectBinary(T& a_Object) {
  std::stringstream ss;
  {
    cereal::BinaryOutputArchive archive(ss);
    archive(a_Object);
  }
  return ss.str();
}

//-----------------------------------------------------------------------------
template <typename T>
inline void DeserializeObjectBinary(const void* data, size_t size, T& object) {
  std::istringstream buffer(std::string(static_cast<const char*>(data), size));
  cereal::BinaryInputArchive inputAr(buffer);
  inputAr(object);
}

//-----------------------------------------------------------------------------
template <typename T>
inline void DeserializeObjectBinary(const std::string& data, T& object) {
  std::istringstream buffer(data);
  cereal::BinaryInputArchive inputAr(buffer);
  inputAr(object);
}

//-----------------------------------------------------------------------------
#define ORBIT_SIZE_SCOPE(x) ScopeCounter counter(x)

#define ORBIT_NVP_VAL(v, x)               \
  do {                                    \
    if (a_Version >= v) {                 \
      a_Archive(cereal::make_nvp(#x, x)); \
    }                                     \
  } while (0)

#define ORBIT_NVP_DEBUG(v, x)             \
  do {                                    \
    if (a_Version >= v) {                 \
      ORBIT_SIZE_SCOPE(#x);               \
      a_Archive(cereal::make_nvp(#x, x)); \
    }                                     \
  } while (0)

#define ORBIT_SERIALIZATION_TEMPLATE_INST_WSTRING(x)                           \
  template void x::serialize<cereal::BinaryOutputArchive>(                     \
      cereal::BinaryOutputArchive & a_Archive, std::uint32_t const a_Version); \
  template void x::serialize<cereal::BinaryInputArchive>(                      \
      cereal::BinaryInputArchive & a_Archive, std::uint32_t const a_Version);

#define ORBIT_SERIALIZATION_TEMPLATE_INST(x)                                   \
  template void x::serialize<cereal::BinaryOutputArchive>(                     \
      cereal::BinaryOutputArchive & a_Archive, std::uint32_t const a_Version); \
  template void x::serialize<cereal::BinaryInputArchive>(                      \
      cereal::BinaryInputArchive & a_Archive, std::uint32_t const a_Version);

#define ORBIT_SERIALIZE_WSTRING(x, v)           \
  CEREAL_CLASS_VERSION(x, v);                   \
  ORBIT_SERIALIZATION_TEMPLATE_INST_WSTRING(x); \
  template <class Archive>                      \
  void x::serialize(Archive& a_Archive, std::uint32_t const a_Version)

#define ORBIT_SERIALIZE(x, v)           \
  CEREAL_CLASS_VERSION(x, v);           \
  ORBIT_SERIALIZATION_TEMPLATE_INST(x); \
  template <class Archive>              \
  void x::serialize(Archive& a_Archive, std::uint32_t const a_Version)

namespace OrbitCore {
template <typename Archive>
struct is_input_archive
    : std::integral_constant<
          bool,
          std::is_base_of_v<cereal::detail::InputArchiveBase,
                            cereal::traits::detail::decay_archive<Archive>>> {};
template <typename Archive>
inline constexpr bool is_input_archive_v = is_input_archive<Archive>::value;
}  // namespace OrbitCore

#ifdef ORBIT_FUZZING
namespace cereal {
// This overload takes precedence over the one provided by cereal, so it allows
// us to change the behaviour. Of course, that's a (temporary) hack and will be
// removed as soon as we remove cereal from the codebase. It only applies to the
// fuzz testing build.
template <class T>
void CEREAL_SERIALIZE_FUNCTION_NAME(BinaryInputArchive& ar, SizeTag<T>& t) {
  ar(t.size);
  if (t.size > 100 * 1024 * 1024) {  // 100 MiB
    throw Exception("size limit reached!");
  }
}
}  // namespace cereal
#endif
