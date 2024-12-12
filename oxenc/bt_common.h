#pragma once
#include "span.h"

namespace oxenc::detail {

template <typename T>
concept signfunc_output = const_span_convertible<T> || std::is_convertible_v<T, std::string_view>;

template <typename T>
concept signverify_func_input = detail::char_view_type<T> || const_span_type<T>;

template <typename Func, typename F = std::remove_reference_t<Func>>
concept lambda_function =
        !(std::is_function_v<F> || std::is_pointer_v<F> || std::is_member_pointer_v<F>);

template <typename>
struct function_traits;

template <typename Func>
struct function_traits
        : public function_traits<decltype(&std::remove_reference_t<Func>::operator())> {};

template <typename Class, typename Return, typename... arg>
struct function_traits<Return (Class::*)(arg...) const> : function_traits<Return (*)(arg...)> {};

template <typename Return, typename... arg>
struct function_traits<Return (*)(arg...)> {
    using arguments = std::tuple<arg...>;

    template <std::size_t Index>
    using argument_type =
            typename std::remove_cvref_t<typename std::tuple_element<Index, arguments>::type>;

    using return_type = Return;

    static constexpr std::size_t arity = sizeof...(arg);
};

template <typename Func, typename F = std::remove_reference_t<Func>, typename... arg>
concept sign_func_hook = lambda_function<F> && (signverify_func_input<arg> && ...) &&
                         signfunc_output<typename function_traits<F>::return_type>;

template <typename Func, typename F = std::remove_reference_t<Func>, typename... arg>
concept void_return_func = lambda_function<F> && (signverify_func_input<arg> && ...) &&
                           std::is_void_v<typename function_traits<F>::return_type>;

}  // namespace oxenc::detail
