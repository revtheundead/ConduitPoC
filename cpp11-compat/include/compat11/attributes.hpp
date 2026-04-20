#ifndef COMPAT11_ATTRIBUTES_HPP
#define COMPAT11_ATTRIBUTES_HPP

#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(nodiscard)
#    define CPP11_NODISCARD [[nodiscard]]
#  endif
#  if __has_cpp_attribute(maybe_unused)
#    define CPP11_MAYBE_UNUSED [[maybe_unused]]
#  endif
#endif

#ifndef CPP11_NODISCARD
#  if defined(__GNUC__) || defined(__clang__)
#    define CPP11_NODISCARD __attribute__((warn_unused_result))
#  else
#    define CPP11_NODISCARD
#  endif
#endif

#ifndef CPP11_MAYBE_UNUSED
#  if defined(__GNUC__) || defined(__clang__)
#    define CPP11_MAYBE_UNUSED __attribute__((unused))
#  else
#    define CPP11_MAYBE_UNUSED
#  endif
#endif

#endif
