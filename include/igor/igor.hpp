// Copyright 2018 Francesco Biscani
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef IGOR_IGOR_HPP
#define IGOR_IGOR_HPP

#include <cstddef>
#include <initializer_list>
#include <tuple>
#include <type_traits>
#include <utility>

namespace igor
{

inline namespace detail
{

// Handy alias.
template <typename T>
using uncvref_t = ::std::remove_cv_t<::std::remove_reference_t<T>>;

// The value returned by named_argument's assignment operator.
// T will always be a reference of some kind.
template <typename Tag, typename T>
struct tagged_container {
    static_assert(::std::is_reference_v<T>, "T must always be a reference.");
    using tag_type = Tag;
    T value;
};

} // namespace detail

// Class to represent a named argument.
template <typename Tag>
struct named_argument {
    // NOTE: make sure this does not interfere with the copy/move assignment operators.
    template <typename T, ::std::enable_if_t<!::std::is_same_v<named_argument, uncvref_t<T>>, int> = 0>
    auto operator=(T &&x) const
    {
        return tagged_container<Tag, T &&>{::std::forward<T>(x)};
    }

    // Add overloads for std::initializer_list as well.
    template <typename T>
    auto operator=(const ::std::initializer_list<T> &l) const
    {
        return tagged_container<Tag, const ::std::initializer_list<T> &>{l};
    }
    template <typename T>
    auto operator=(::std::initializer_list<T> &l) const
    {
        return tagged_container<Tag, ::std::initializer_list<T> &>{l};
    }
    template <typename T>
    auto operator=(::std::initializer_list<T> &&l) const
    {
        return tagged_container<Tag, ::std::initializer_list<T> &&>{::std::move(l)};
    }
    template <typename T>
    auto operator=(const ::std::initializer_list<T> &&l) const
    {
        return tagged_container<Tag, const ::std::initializer_list<T> &&>{::std::move(l)};
    }
};

// Type representing a named argument which
// was not provided in a function call.
struct not_provided_t {
};

// Non-provided named arguments will return a const reference
// to this global object.
inline constexpr not_provided_t not_provided;

inline namespace detail
{

// Implementation of parsers' constructor.
inline auto build_parser_tuple()
{
    return ::std::tuple{};
}

template <typename Arg0, typename... Args>
inline auto build_parser_tuple(const Arg0 &, const Args &... args)
{
    return build_parser_tuple(args...);
}

template <typename Tag, typename T, typename... Args>
inline auto build_parser_tuple(const tagged_container<Tag, T> &arg0, const Args &... args)
{
    return ::std::tuple_cat(::std::forward_as_tuple(arg0), build_parser_tuple(args...));
}

// Type trait to detect if T is a tagged container with tag Tag (and any type as second parameter).
template <typename Tag, typename T>
struct is_tagged_container : ::std::false_type {
};

template <typename Tag, typename T>
struct is_tagged_container<Tag, tagged_container<Tag, T>> : ::std::true_type {
};

// Type trait to detect if T is a tagged container (regardless of the tag type or the type
// of the second parameter).
template <typename T>
struct is_tagged_container_any : ::std::false_type {
};

template <typename Tag, typename T>
struct is_tagged_container_any<tagged_container<Tag, T>> : ::std::true_type {
};

} // namespace detail

// NOTE: implement some of the parser functionality as free functions,
// which will then be wrapped by static constexpr member functions in
// the parser class. These free functions can be used where a parser
// object is not available (e.g., in a requires clause).
template <typename... Args, typename Tag>
constexpr bool has([[maybe_unused]] const named_argument<Tag> &narg)
{
    return (... || is_tagged_container<Tag, uncvref_t<Args>>::value);
}

template <typename... Args, typename... Tags>
constexpr bool has_all(const named_argument<Tags> &... nargs)
{
    return (... && ::igor::has<Args...>(nargs));
}

template <typename... Args, typename... Tags>
constexpr bool has_any(const named_argument<Tags> &... nargs)
{
    return (... || ::igor::has<Args...>(nargs));
}

template <typename... Args>
constexpr bool has_unnamed_arguments()
{
    return (... || !is_tagged_container_any<uncvref_t<Args>>::value);
}

template <typename... Args, typename... Tags>
constexpr bool has_other_than(const named_argument<Tags> &... nargs)
{
    // NOTE: the first fold expression will return how many of the nargs
    // are in the pack. The second fold expression will return the total number
    // of named arguments in the pack.
    return (std::size_t(0) + ... + static_cast<std::size_t>(::igor::has<Args...>(nargs)))
           < (std::size_t(0) + ... + static_cast<std::size_t>(is_tagged_container_any<uncvref_t<Args>>::value));
}

// Parser for named arguments in a function call.
template <typename... ParseArgs>
class parser
{
    using tuple_t = decltype(build_parser_tuple(::std::declval<const ParseArgs &>()...));

public:
    explicit parser(const ParseArgs &... parse_args) : m_nargs(build_parser_tuple(parse_args...)) {}

private:
    // Fetch the value associated to the input named
    // argument narg. If narg is not present, this will
    // return a const ref to a global not_provided_t object.
    template <::std::size_t I, typename Tag>
    decltype(auto) fetch_one_impl([[maybe_unused]] const named_argument<Tag> &narg) const
    {
        if constexpr (I == ::std::tuple_size_v<tuple_t>) {
            return static_cast<const not_provided_t &>(not_provided);
        } else if constexpr (::std::is_same_v<typename uncvref_t<::std::tuple_element_t<I, tuple_t>>::tag_type, Tag>) {
            if constexpr (::std::is_rvalue_reference_v<decltype(::std::get<I>(m_nargs).value)>) {
                return ::std::move(::std::get<I>(m_nargs).value);
            } else {
                return ::std::get<I>(m_nargs).value;
            }
        } else {
            return fetch_one_impl<I + 1u>(narg);
        }
    }

public:
    // Get references to the values associated to the input named arguments.
    template <typename... Tags>
    decltype(auto) operator()([[maybe_unused]] const named_argument<Tags> &... nargs) const
    {
        if constexpr (sizeof...(Tags) == 0u) {
            return;
        } else if constexpr (sizeof...(Tags) == 1u) {
            return fetch_one_impl<0>(nargs...);
        } else {
            return ::std::forward_as_tuple(fetch_one_impl<0>(nargs)...);
        }
    }
    // Check if the input named argument na is present in the parser.
    template <typename Tag>
    static constexpr bool has(const named_argument<Tag> &narg)
    {
        return ::igor::has<ParseArgs...>(narg);
    }
    // Check if all the input named arguments nargs are present in the parser.
    template <typename... Tags>
    static constexpr bool has_all(const named_argument<Tags> &... nargs)
    {
        return ::igor::has_all<ParseArgs...>(nargs...);
    }
    // Check if at least one of the input named arguments nargs is present in the parser.
    template <typename... Tags>
    static constexpr bool has_any(const named_argument<Tags> &... nargs)
    {
        return ::igor::has_any<ParseArgs...>(nargs...);
    }
    // Detect the presence of unnamed arguments.
    static constexpr bool has_unnamed_arguments()
    {
        return ::igor::has_unnamed_arguments<ParseArgs...>();
    }
    // Check if the parser contains named arguments other than nargs.
    template <typename... Tags>
    static constexpr bool has_other_than(const named_argument<Tags> &... nargs)
    {
        return ::igor::has_other_than<ParseArgs...>(nargs...);
    }

private:
    tuple_t m_nargs;
};

} // namespace igor

// Handy macro (ew) for the definition of a named argument.
#define IGOR_MAKE_NAMED_ARGUMENT(name)                                                                                 \
    struct name##_tag {                                                                                                \
    };                                                                                                                 \
    inline constexpr auto name = ::igor::named_argument<name##_tag> {}

#endif
