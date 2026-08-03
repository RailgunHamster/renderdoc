#pragma once

#include <cstddef>

// Newer MSVC (VS2022 17.13+ / VS18) removed the stdext checked-iterator helpers
// that Qt 5.15 still references. Provide compatible shims that degrade to plain
// pointers, which is all the affected Qt code actually needs.

namespace stdext
{
template <typename _T>
_T *make_checked_array_iterator(_T *_First, size_t _Size)
{
  return _First;
}

template <typename _T>
_T *make_unchecked_array_iterator(_T *_First)
{
  return _First;
}
}
