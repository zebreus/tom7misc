
#ifndef _CC_LIB_MAP_UTIL_H
#define _CC_LIB_MAP_UTIL_H

// This returns a copy of the value (to avoid danging references to
// the default) so it should generally be used when def is a small
// value.
template<class M>
inline auto FindOrDefault(const M &m, const typename M::key_type &k,
                          typename M::mapped_type def) ->
  typename M::mapped_type {
  auto it = m.find(k);
  if (it == m.end()) return def;
  else return it->second;
}

#endif
