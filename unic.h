#pragma once

// Written by Ayxan Haqverdili
// 2021 June 04

#include <cstddef>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <bit>

namespace unic
{

namespace concepts
{
/// Some helper concept-combinations
/// ..._for<T1, T2> -> T1 contains the T2
template <class Iter, class Type>
concept iterator_for = ::std::same_as<::std::decay_t<::std::iter_value_t<Iter>>, Type>;

template <class Iter, class Type>
concept forward_iterator_for = ::std::forward_iterator<Iter> &&iterator_for<Iter, Type>;

template <class Iter, class Type>
concept input_iterator_for = ::std::input_iterator<Iter> &&iterator_for<Iter, Type>;

template <class Range, class Type>
concept range_for =
    ::std::ranges::range<Range> && ::std::same_as<::std::decay_t<::std::ranges::range_value_t<Range>>, Type>;

template <class Range, class Type>
concept sized_range_for = ::std::ranges::sized_range<Range> &&range_for<Range, Type>;

template <class SizedInputRange, class Type>
concept sized_input_range_for = sized_range_for<SizedInputRange, Type> && ::std::ranges::input_range<SizedInputRange>;

template <class SizedInputRange, class Type>
concept sized_forward_range_for =
    sized_range_for<SizedInputRange, Type> && ::std::ranges::forward_range<SizedInputRange>;

template <class InputRange, class Type>
concept input_range_for = ::std::ranges::input_range<InputRange> &&range_for<InputRange, Type>;
} // namespace concepts

// Exception classes
struct utf_error : ::std::runtime_error
{
    utf_error(::std::string const &msg)
        : ::std::runtime_error(msg)
    {
    }
};

// This exception contains the point where error happened
template <class src_iter>
struct utf_positioned_error : utf_error
{
    [[no_unique_address]] src_iter error_position{};

    utf_positioned_error(src_iter err_pos, ::std::string const &msg)
        : error_position(::std::move(err_pos))
        , utf_error(::std::move(msg))
    {
    }
};

// utf8 to code points
template <concepts::forward_iterator_for<char8_t> src_iter, ::std::sized_sentinel_for<src_iter> src_end_iter>
class from_utf8_range final
{
  private:
    [[no_unique_address]] src_iter m_begin{};
    [[no_unique_address]] src_end_iter m_end{};

  public:
    struct iterator final // forward_iterator
    {
      private:
        friend class from_utf8_range;

        [[no_unique_address]] src_iter m_begin{};
        [[no_unique_address]] src_end_iter m_end{};

        constexpr iterator(src_iter begin, src_end_iter end) noexcept
            : m_begin(::std::move(begin))
            , m_end(::std::move(end))
        {
        }

        // returns -1 on fail
        [[nodiscard]] static constexpr int compute_byte_count(char8_t const header) noexcept
        {
            auto const cnt = ::std::countl_one(static_cast<unsigned char>(header));
            if (cnt == 0)
                return 1;

            if (cnt < 2 || cnt > 6)
                return -1;

            return cnt;
        }

      public:
        constexpr iterator() = default; // required for the iterator concept apparently

        using iterator_category = ::std::forward_iterator_tag;
        using difference_type = ::std::ptrdiff_t;
        using value_type = char32_t;

        [[maybe_unused]] constexpr auto operator++() -> iterator &
        {
            auto const cnt = compute_byte_count(*m_begin);
            if (cnt == -1 || cnt > m_end - m_begin)
                throw utf_positioned_error(m_begin, "Length in header byte is wrong");

            ::std::advance(m_begin, cnt);

            return *this;
        }

        [[nodiscard]] constexpr auto operator++(int) -> iterator
        {
            auto copy = *this;
            ++*this;
            return copy;
        }

        [[nodiscard]] constexpr auto operator*() const -> char32_t
        {
            auto const cnt = compute_byte_count(*m_begin);
            if (cnt == -1 || cnt > m_end - m_begin)
                throw utf_positioned_error(m_begin, "Length in header byte is wrong");

            char32_t code_point = 0;

            if (cnt == 1) // ascii
                code_point = *m_begin;
            else
            {
                // extract trailing bits
                auto begin = m_begin;
                code_point = static_cast<char32_t>(*begin++ & (static_cast<char8_t>(~0u) >> (cnt + 1)));

                // extract rest of the bytes
                for (int i = 1; i < cnt; ++i)
                {
                    if (*begin < 0x80 || 0xBF < *begin)
                        throw utf_positioned_error(begin, "Illegal trail byte");

                    code_point = (code_point << 6) | (*begin & 0x3F);
                    ++begin;
                }
            }
            return code_point;
        }

        [[nodiscard]] constexpr auto operator==(iterator const &other) const noexcept -> bool
        {
            return m_begin == other.m_begin;
        }
    };

  public:
    constexpr from_utf8_range(src_iter begin, src_end_iter end) noexcept
        : m_begin(::std::move(begin))
        , m_end(::std::move(end))
    {
    }

    template <concepts::sized_range_for<char8_t> u8range>
    constexpr from_utf8_range(u8range const &range) noexcept
        : from_utf8_range(::std::ranges::begin(range), ::std::ranges::end(range))
    {
    }

    // all iterators are const
    [[nodiscard]] constexpr auto begin() const noexcept { return iterator(m_begin, m_end); }
    [[nodiscard]] constexpr auto end() const noexcept { return iterator{m_end, m_end}; }
    [[nodiscard]] constexpr auto cbegin() const noexcept { return begin(); }
    [[nodiscard]] constexpr auto cend() const noexcept { return end(); }
};

template <concepts::sized_range_for<char8_t> u8range>
from_utf8_range(u8range const &range) noexcept->from_utf8_range<::std::decay_t<decltype(::std::ranges::begin(range))>,
                                                                ::std::decay_t<decltype(::std::ranges::end(range))>>;

// Code points to utf16
template <::std::output_iterator<char16_t> out_iter>
class to_utf16_iter final
{
  private:
    [[no_unique_address]] out_iter m_iter{};

    // Outputs a code-point to a UTF-16 output iterator
    constexpr void append(char32_t code_point)
    {
        if (code_point <= 0xFFFF)
        {
            *m_iter++ = static_cast<char16_t>(code_point);
        }
        else if (code_point <= 0x10FFFF)
        {
            code_point -= 0x10000;
            *m_iter++ = static_cast<char16_t>((code_point >> 10) + 0xD800);
            *m_iter++ = static_cast<char16_t>((code_point & 0x3FF) + 0xDC00);
        }
        else
        {
            throw utf_positioned_error(m_iter, "Out of UTF-16 range");
        }
    }

    struct proxy_assigner
    {
      private:
        friend class to_utf16_iter;
        [[no_unique_address]] to_utf16_iter *m_parent;

        proxy_assigner(to_utf16_iter *parent) noexcept
            : m_parent{parent}
        {
        }

        proxy_assigner(::std::nullptr_t) = delete;

      public:
        [[maybe_unused]] constexpr auto operator=(char32_t const code_point) const -> proxy_assigner const &
        {
            m_parent->append(code_point);
            return *this;
        }

        template <class T>
        proxy_assigner &operator=(T) const = delete;
    };

  public:
    to_utf16_iter(out_iter iter) noexcept
        : m_iter(::std::move(iter))
    {
    }

    to_utf16_iter() = default;

    using iterator_category = ::std::output_iterator_tag;
    using difference_type = ::std::ptrdiff_t;

    // Incrementing is no-op for output iterators. Actual incrementing is done on
    // write
    [[maybe_unused]] constexpr auto operator++() noexcept -> to_utf16_iter & { return *this; }
    [[nodiscard]] constexpr auto operator++(int) noexcept -> to_utf16_iter { return *this; }

    [[nodiscard]] constexpr auto operator*() noexcept -> proxy_assigner { return {this}; }
};

template <concepts::input_iterator_for<char32_t> input_beg, ::std::sentinel_for<input_beg> input_end>
[[nodiscard]] constexpr ::std::ptrdiff_t to_utf16_size(input_beg beg, input_end const end)
{
    ::std::ptrdiff_t size = 0;

    for (; beg != end; ++beg)
    {
        auto const code_point = *beg;
        if (code_point <= 0xFFFF)
            size += 1;
        else if (code_point <= 0x10FFFF)
            size += 2;
        else
            throw utf_positioned_error(beg, "Out of UTF-16 range");
    }

    return size;
}

template <concepts::input_range_for<char32_t> u32range>
[[nodiscard]] constexpr ::std::ptrdiff_t to_utf16_size(u32range const &range)
{
    return to_utf16_size(::std::ranges::begin(range), ::std::ranges::end(range));
}

template <concepts::forward_iterator_for<char8_t> input_beg, ::std::sized_sentinel_for<input_beg> input_end>
[[nodiscard]] constexpr ::std::ptrdiff_t to_utf16_size(input_beg beg, input_end const end)
{
    return to_utf16_size(from_utf8_range{beg, end});
}

template <concepts::sized_forward_range_for<char8_t> u8range>
[[nodiscard]] constexpr ::std::ptrdiff_t to_utf16_size(u8range const &range)
{
    return to_utf16_size(from_utf8_range{range});
}

template <concepts::forward_iterator_for<char8_t> u8beg, ::std::sized_sentinel_for<u8beg> u8end,
          ::std::output_iterator<char32_t> code_point_out>
constexpr void to_utf32(u8beg beg, u8end end, code_point_out out)
{
    ::std::ranges::copy(from_utf8_range{beg, end}, out);
}

template <concepts::forward_iterator_for<char8_t> u8beg, ::std::sized_sentinel_for<u8beg> u8end,
          ::std::output_iterator<char16_t> code_point_out>
constexpr void to_utf16(u8beg beg, u8end end, code_point_out out)
{
    to_utf32(beg, end, to_utf16_iter{out});
}

template <concepts::sized_input_range_for<char8_t> u8range, ::std::output_iterator<char32_t> code_point_out_iter>
constexpr void to_utf32(u8range const &range, code_point_out_iter out)
{
    from_utf8_range rng{range};
    to_utf32(::std::ranges::begin(rng), ::std::ranges::end(rng), out);
}

template <concepts::sized_input_range_for<char8_t> u8range, ::std::output_iterator<char16_t> code_point_out>
constexpr void to_utf16(u8range const &range, code_point_out out)
{
    to_utf16(::std::ranges::begin(range), ::std::ranges::end(range), out);
}

} // namespace unic
