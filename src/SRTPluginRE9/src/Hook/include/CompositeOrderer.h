#ifndef SRTPLUGINRE9_COMPOSITEORDERER_H
#define SRTPLUGINRE9_COMPOSITEORDERER_H

#include <functional>
#include <tuple>

/// @brief A structure to apply a chain of ordering operations.
/// @tparam ...TComparators The comparator types to apply over the elements.
template <typename... TComparators>
struct CompositeOrderer
{
	std::tuple<TComparators...> comparators;

	constexpr CompositeOrderer(TComparators... comps) : comparators{std::move(comps)...} {}

	constexpr auto operator()(const auto &a, const auto &b) const -> bool
	{
		return std::apply([&](const auto &...comps)
		                  {
            // Try each comparator in order. The first non-tie decides.
            bool result = false;
            bool found  = false;
            ((found || (comps(a, b) ? (result = true,  found = true) : 
                        comps(b, a) ? (result = false, found = true) : false)), ...);
            return result; }, comparators);
	}

	/// @brief Continuation of a prior OrderBy-operation wherein we are further ordering elements in ascending (smallest to largest) order.
	/// @tparam TProjection The projection type.
	/// @tparam TComparator The comparator we're applying.
	/// @param proj The projection function to apply to the elements.
	/// @param comp The comparator to apply to the projection function.
	/// @return A CompositeOrderer to chain other operations on or to apply to a sort.
	template <typename TProjection, typename TComparator = std::ranges::less>
	constexpr auto ThenBy(TProjection proj, TComparator comp = {}) const
	{
		auto next = [proj = std::move(proj), comp = std::move(comp)](const auto &a, const auto &b)
		{
			return comp(proj(a), proj(b));
		};
		return std::apply([&](const auto &...existing)
		                  { return CompositeOrderer<TComparators..., decltype(next)>{existing..., std::move(next)}; }, comparators);
	}

	/// @brief Continuation of a prior OrderBy-operation wherein we are further ordering elements in descending (largest to smallest) order.
	/// @tparam TProjection The projection type.
	/// @param proj The projection function to apply to the elements.
	/// @return A CompositeOrderer to chain other operations on or to apply to a sort.
	template <typename TProjection>
	constexpr auto ThenByDescending(TProjection proj) const
	{
		return ThenBy(std::move(proj), std::ranges::greater{});
	}
};

/// @brief Orders elements in ascending (smallest to largest) order.
/// @tparam TProjection The projection type.
/// @tparam TComparator The comparator we're applying.
/// @param proj The projection function to apply to the elements.
/// @param comp The comparator to apply to the projection function.
/// @return A CompositeOrderer to chain other operations on or to apply to a sort.
template <typename TProjection, typename TComparator = std::ranges::less>
constexpr auto OrderBy(TProjection proj, TComparator comp = {})
{
	auto first = [proj = std::move(proj), comp = std::move(comp)](const auto &a, const auto &b)
	{
		return comp(proj(a), proj(b));
	};
	return CompositeOrderer<decltype(first)>{std::move(first)};
}

/// @brief Orders elements in descending (largest to smallest) order.
/// @tparam TProjection The projection type.
/// @param proj The projection function to apply to the elements.
/// @return A CompositeOrderer to chain other operations on or to apply to a sort.
template <typename TProjection>
constexpr auto OrderByDescending(TProjection proj)
{
	return OrderBy(std::move(proj), std::ranges::greater{});
}

#endif
