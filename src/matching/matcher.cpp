// Copyright (C) 2020-2021 Adrian Wöltche
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program. If not, see https://www.gnu.org/licenses/.

#include "matcher.hpp"

#include <geometry/track/types.hpp>
#include <geometry/network/types.hpp>
#include <geometry/substring.hpp>
#include <geometry/util.hpp>
#include <io/csv_exporter.hpp>
#include <environment/environments/hmm.hpp>
#include <learning/algorithms/viterbi.hpp>

namespace map_matching_2::matching {

    template<typename Network, typename Track>
    matcher<Network, Track>::matcher(const Network &network, bool single_threaded)
            : matcher{network, single_threaded ? 1 : boost::thread::hardware_concurrency()} {}

    template<typename Network, typename Track>
    matcher<Network, Track>::matcher(const Network &network, const std::uint32_t threads)
            : network{network}, _single_threaded{threads == 1}, _matching_thread_pool{threads} {}

    template<typename Network, typename Track>
    std::vector<typename matcher<Network, Track>::candidate_type> matcher<Network, Track>::candidate_search_nearest(
            const Track &track, const std::size_t number, const bool with_reverse, const bool with_adjacent,
            const bool candidate_adoption_siblings, const bool candidate_adoption_nearby,
            const bool candidate_adoption_reverse) {
        std::vector<std::multiset<candidate_edge_type, candidate_edge_distance_comparator_type>> candidate_edge_sets{
                track.measurements.size(), std::multiset<candidate_edge_type, candidate_edge_distance_comparator_type>{
                        &candidate_edge_type::distance_comparator}};

        std::vector<std::size_t> numbers;
        numbers.reserve(track.measurements.size());

        for (std::size_t i = 0; i < track.measurements.size(); ++i) {
            numbers.emplace_back(_candidate_search_nearest_measurement(
                    candidate_edge_sets[i], 1, track, i, number, with_reverse, with_adjacent));
        }

        std::vector<candidate_type> candidates;
        candidates.reserve(candidate_edge_sets.size());
        _finalize_candidates_nearest(
                candidates, 1, track, numbers, candidate_edge_sets, candidate_adoption_siblings,
                candidate_adoption_nearby, candidate_adoption_reverse);

        return candidates;
    }

    template<typename Network, typename Track>
    std::vector<typename matcher<Network, Track>::candidate_type> matcher<Network, Track>::candidate_search_buffer(
            const Track &track, const std::size_t buffer_points,
            const double buffer_radius, const double buffer_upper_radius, const double buffer_lower_radius,
            const bool adaptive_radius, const bool candidate_adoption_siblings, const bool candidate_adoption_nearby,
            const bool candidate_adoption_reverse) {
        std::vector<std::multiset<candidate_edge_type, candidate_edge_distance_comparator_type>> candidate_edge_sets{
                track.measurements.size(), std::multiset<candidate_edge_type, candidate_edge_distance_comparator_type>{
                        &candidate_edge_type::distance_comparator}};

        std::vector<double> radii;
        radii.reserve(track.measurements.size());

        for (std::size_t i = 0; i < track.measurements.size(); ++i) {
            radii.emplace_back(_candidate_search_buffer_measurement(
                    candidate_edge_sets[i], 1, track, i, buffer_points, buffer_radius, buffer_upper_radius,
                    buffer_lower_radius, adaptive_radius));
        }

        std::vector<candidate_type> candidates;
        candidates.reserve(candidate_edge_sets.size());
        _finalize_candidates_buffer(
                candidates, 1, track, radii, candidate_edge_sets, candidate_adoption_siblings,
                candidate_adoption_nearby,
                candidate_adoption_reverse);

        return candidates;
    }

    template<typename Network, typename Track>
    void matcher<Network, Track>::resize_candidates_nearest(
            const Track &track, std::vector<candidate_type> &candidates, const std::vector<std::size_t> &positions,
            const std::size_t round, const bool with_reverse, const bool with_adjacent,
            const bool candidate_adoption_siblings, const bool candidate_adoption_nearby,
            const bool candidate_adoption_reverse) {
        std::vector<std::multiset<candidate_edge_type, candidate_edge_distance_comparator_type>> candidate_edge_sets{
                candidates.size(), std::multiset<candidate_edge_type, candidate_edge_distance_comparator_type>{
                        &candidate_edge_type::distance_comparator}};

        std::vector<std::size_t> numbers;
        numbers.reserve(candidates.size());

        for (std::size_t i = 0; i < candidates.size(); ++i) {
            auto &candidate = candidates[i];
            numbers.emplace_back(candidate.number);
            auto &candidate_edges = candidate.edges;
            auto &candidate_edge_set = candidate_edge_sets[i];
            for (auto &candidate_edge: candidate_edges) {
                candidate_edge_set.emplace(std::move(candidate_edge));
            }
        }

        for (const auto i: positions) {
            numbers[i] = _candidate_search_nearest_measurement(
                    candidate_edge_sets[i], round, track, i, numbers[i] * 2, with_reverse, with_adjacent);
        }

        _finalize_candidates_nearest(
                candidates, round, track, numbers, candidate_edge_sets, candidate_adoption_siblings,
                candidate_adoption_nearby, candidate_adoption_reverse);
    }

    template<typename Network, typename Track>
    void matcher<Network, Track>::resize_candidates_buffer(
            const Track &track, std::vector<candidate_type> &candidates, const std::vector<std::size_t> &positions,
            const std::size_t round, const std::size_t buffer_points, const double buffer_upper_radius,
            const double buffer_lower_radius, const bool adaptive_radius, const bool adaptive_resize,
            const bool candidate_adoption_siblings, const bool candidate_adoption_nearby,
            const bool candidate_adoption_reverse) {
        std::vector<std::multiset<candidate_edge_type, candidate_edge_distance_comparator_type>> candidate_edge_sets{
                candidates.size(), std::multiset<candidate_edge_type, candidate_edge_distance_comparator_type>{
                        &candidate_edge_type::distance_comparator}};

        std::vector<double> radii;
        radii.reserve(candidates.size());

        for (std::size_t i = 0; i < candidates.size(); ++i) {
            auto &candidate = candidates[i];
            radii.emplace_back(candidate.radius);
            auto &candidate_edges = candidate.edges;
            auto &candidate_edge_set = candidate_edge_sets[i];
            for (auto &candidate_edge: candidate_edges) {
                candidate_edge_set.emplace(std::move(candidate_edge));
            }
        }

        std::map<std::size_t, double> position_map;
        for (const auto i: positions) {
            position_map.emplace(i, radii[i] * 2);
        }

        if (adaptive_resize) {
            using index_value_type = std::pair<point_type, std::size_t>;

            std::vector<index_value_type> measurement_index_values;
            measurement_index_values.reserve(track.measurements.size());
            for (std::size_t i = 0; i < track.measurements.size(); ++i) {
                const auto &measurement = track.measurements[i];
                measurement_index_values.emplace_back(std::pair{measurement.point, i});
            }

            boost::geometry::index::rtree<index_value_type, boost::geometry::index::rstar<16>>
                    measurements_index{std::move(measurement_index_values)};

            for (const auto i: positions) {
                const auto &measurement = track.measurements[i];
                auto &edge_set = candidate_edge_sets[i];
                auto edge_set_it = edge_set.crbegin();
                const auto longest_distance = edge_set_it->distance;

                const auto buffer = _create_buffer(measurement.point, radii[i] * 2);

                std::vector<index_value_type> results;
                measurements_index.query(boost::geometry::index::intersects(buffer), std::back_inserter(results));

                for (const auto &result: results) {
                    const auto j = result.second;
                    position_map.emplace(j, radii[j] * 2);
                }
            }
        }

        for (const auto &pos: position_map) {
            const auto i = pos.first;
            const auto radius = pos.second;
            // disable adaptive radius this time as we used it the previous time if it was enabled
            radii[i] = _candidate_search_buffer_measurement(
                    candidate_edge_sets[i], round, track, i, buffer_points, radius, buffer_upper_radius,
                    buffer_lower_radius, false);
        }

        _finalize_candidates_buffer(
                candidates, round, track, radii, candidate_edge_sets, candidate_adoption_siblings,
                candidate_adoption_nearby, candidate_adoption_reverse);
    }

    template<typename Network, typename Track>
    std::vector<std::list<typename Network::vertex_descriptor>> matcher<Network, Track>::shortest_paths(
            const std::vector<candidate_type> &candidates,
            const std::size_t from, const std::size_t source, const std::size_t to, const double max_distance_factor) {
        const auto &candidate_edge_start = candidates[from].edges[source];
        const auto vertex_start = boost::target(candidate_edge_start.edge_descriptor, network.graph);
        const auto &node_start = network.graph[vertex_start];

        distance_type max_distance = geometry::default_float_type<distance_type>::v0;
        std::unordered_set<typename boost::graph_traits<graph_type>::vertex_descriptor> goals;
        for (const auto &candidate_node: candidates[to].nodes) {
            const auto vertex_goal = candidate_node.vertex_descriptor;
            goals.emplace(vertex_goal);

            const auto &node_goal = network.graph[vertex_goal];
            const auto distance = geometry::point_distance(node_start.point, node_goal.point);
            if (distance > max_distance) {
                max_distance = distance;
            }
        }

        _prepare_shortest_path_memory();
        return network.dijkstra_shortest_paths(vertex_start, goals, max_distance_factor, max_distance,
                                               *_vertices, *_predecessors, *_distances, *_colors);
    }

    template<typename Network, typename Track>
    const typename matcher<Network, Track>::route_type &matcher<Network, Track>::route(
            const std::vector<candidate_type> &candidates,
            const std::size_t from, const std::size_t source, const std::size_t to, const std::size_t target,
            const double max_distance_factor) {
        const auto from_target = boost::target(
                candidates[from].edges[source].edge_descriptor, network.graph);
        const auto to_source = boost::source(
                candidates[to].edges[target].edge_descriptor, network.graph);

        route_cache_key_type key{from_target, to_source};

        if (not _route_cache.get()) {
            _route_cache.reset(new route_cache_type{});
        }

        auto search = _route_cache->find(key);
        bool exists = search != _route_cache->end();

        if (exists) {
            return std::cref(search->second);
        } else {
            bool found = false;
            route_cache_iterator_type return_route;

            const auto shortest_paths = this->shortest_paths(candidates, from, source, to, max_distance_factor);
            for (const auto &shortest_path: shortest_paths) {
                route_cache_key_type new_key{shortest_path.front(), shortest_path.back()};

                auto new_search = _route_cache->find(new_key);
                exists = new_search != _route_cache->end();

                if (exists) {
                    if (key == new_key) {
                        return_route = new_search;
                        found = true;
                    }
                } else {
                    auto new_route = network.extract_route(shortest_path);

                    auto inserted = _route_cache->emplace(new_key, std::move(new_route));

                    if (key == new_key) {
                        return_route = inserted.first;
                        found = true;
                    }
                }
            }

            if (found) {
                return std::cref(return_route->second);
            } else {
                route_type no_route{true};
                auto inserted = _route_cache->emplace(key, std::move(no_route));
                return std::cref(inserted.first->second);
            }
        }
    }

    template<typename Network, typename Track>
    void matcher<Network, Track>::clear_route_cache() {
        _route_cache.reset(new route_cache_type{});
    }

    template<typename Network, typename Track>
    typename matcher<Network, Track>::route_type matcher<Network, Track>::candidate_route(
            const std::vector<candidate_type> &candidates,
            const std::size_t from, const std::size_t source, const std::size_t to, const std::size_t target,
            const double max_distance_factor) {
        const auto &candidate_from = candidates[from].edges[source];
        const auto &candidate_to = candidates[to].edges[target];

        const auto &start = candidate_from.to;
        const auto &end = candidate_to.from;

        if (geometry::equals_points(candidate_from.projection_point, candidate_to.projection_point)) {
            // when both measurements point to same candidate, no route is needed as we are already there
            return route_type{false};
        }

        if (candidate_from.edge_descriptor == candidate_to.edge_descriptor) {
            // when both measurements point to the same edge
            if (start.has_length and end.has_length) {
                // when both partial routes exist on the same edge
                const auto &edge = network.graph[candidate_from.edge_descriptor];
                if (edge.has_length and start.length + end.length >= edge.length) {
                    // if both lines overlap (which is only possible when sum of lengths is larger or equal than edge length)
                    if (candidate_from.from.has_length and candidate_to.from.has_length and
                        candidate_from.from.length <= candidate_to.from.length) {
                        // only continue if substring is possible, else routing is needed
//                            std::cout << edge << "\n" << candidate_from << "\n" << candidate_to << std::endl;
                        return route_type{geometry::substring<rich_line_type>(
                                edge, candidate_from.from.length, candidate_to.from.length,
                                candidate_from.projection_point, candidate_to.projection_point,
                                true, true)};
                    }
                }
            }
        }

        route_type route = this->route(candidates, from, source, to, target, max_distance_factor);

        if (not route.is_invalid) {
            route_type start_route{std::cref(start)};
            route_type end_route{std::cref(end)};

            try {
                return route_type::merge({start_route, route, end_route}, false, true);
            } catch (geometry::network::route_merge_exception &exception) {
                // could not merge route, return invalid route
                return route_type{true};
            } catch (geometry::rich_line_merge_exception &exception) {
                // could not merge rich lines in route, return invalid route
                return route_type{true};
            }
        }

        // did not find a route, return invalid route
        return route_type{true};
    }

    template<typename Network, typename Track>
    typename matcher<Network, Track>::route_type matcher<Network, Track>::candidates_route(
            const std::vector<candidate_type> &candidates,
            const std::vector<std::pair<std::size_t, std::size_t>> &policy,
            const bool export_edges, const bool join_merges) {
        std::vector<route_type> routes;
        routes.reserve(not candidates.empty() ? candidates.size() - 1 : 0);
        bool skipped = false;
        for (std::size_t i = 0, j = 1; j < policy.size(); ++i, ++j) {
            const auto &from_policy_pair = policy[i];
            const auto &to_policy_pair = policy[j];

            const auto from = from_policy_pair.first;
            const auto to = to_policy_pair.first;

            if (from == to) {
                // skip detected
                ++i;
                ++j;
                skipped = true;
                continue;
            }

            const auto source = from_policy_pair.second;
            const auto target = to_policy_pair.second;

            route_type route = candidate_route(candidates, from, source, to, target);

            if (join_merges and not skipped and
                not routes.empty() and not route.is_invalid and not route.rich_lines.empty()) {
                route_type &prev_route = routes.back();
                if (not prev_route.is_invalid and not prev_route.rich_lines.empty()) {
                    const rich_line_type &prev_route_back = prev_route.rich_lines.back();
                    if (not prev_route_back.line.empty()) {
                        const point_type &prev_route_last_point = prev_route_back.line.back();
                        const auto point_search = network.point_in_edges(prev_route_last_point);
                        if (not point_search.first) {
                            route_type::join(prev_route, route, true);
                        }
                    }
                }
            }

            if (not route.is_invalid and route.has_length) {
                skipped = false;
                routes.emplace_back(std::move(route));
            }
        }

//        std::cout << "\nRoutes:" << std::endl;
//        for (const auto &route : routes) {
//            std::cout << route.wkt() << std::endl;
//        }

        route_type route = route_type::merge({routes.begin(), routes.end()});

        if (not export_edges) {
            return route;
        } else {
            std::vector<std::reference_wrapper<const rich_line_type>> edges;
            edges.reserve(not candidates.empty() ? candidates.size() - 1 : 0);

            const auto multi_rich_line = route.get_multi_rich_line();
            for (const auto &rich_line: multi_rich_line.rich_lines) {
                for (const auto &rich_segment: rich_line.rich_segments) {
                    if (rich_segment.has_length and rich_segment.has_azimuth) {
                        const auto edge_search = network.fitting_edge(rich_segment.segment);
                        if (edge_search.first) {
                            const auto &edge = network.graph[edge_search.second];
                            if (edges.empty() or &edges.back().get() != &edge) {
                                edges.emplace_back(std::cref(edge));
                            }
                        }
                    }
                }
            }

//            std::cout << "\nEdges:" << std::endl;
//            for (const rich_line_type &edge : edges) {
//                std::cout << edge.wkt() << std::endl;
//            }

            return route_type{std::move(edges)};
        }
    }

    template<typename Network, typename Track>
    std::vector<typename matcher<Network, Track>::candidate_type> matcher<Network, Track>::candidates_policy(
            const std::vector<candidate_type> &candidates,
            const std::vector<std::pair<std::size_t, std::size_t>> &policy) {
        std::vector<candidate_type> candidates_policy;
        candidates_policy.reserve(candidates.size());
        for (const auto &policy_pair: policy) {
            const auto candidate_index = policy_pair.first;
            const auto edge_index = policy_pair.second;

            const auto &candidate = candidates[candidate_index];
            if (not candidate.edges.empty()) {
                candidates_policy.emplace_back(candidate_type{
                        *candidate.track, candidate.index, candidate.next_equal,
                        {}, {candidate.edges[edge_index]}});
            }
        }

        return candidates_policy;
    }

    template<typename Network, typename Track>
    void matcher<Network, Track>::save_candidates(std::string filename,
                                                  const std::vector<candidate_type> &candidates) const {
        io::csv_exporter<',', '"'> csv{std::move(filename)};
        bool first = true;
        for (const auto &candidate: candidates) {
            if (first) {
                csv << candidate.header();
                first = false;
            }
            const auto rows = candidate.rows();
            for (const auto &row: rows) {
                csv << row;
            }
        }
    }

    template<typename Network, typename Track>
    std::vector<std::set<defect>> matcher<Network, Track>::detect(
            const Track &track, const bool detect_duplicates, const bool detect_forward_backward) const {
        return _detector.detect(track, detect_duplicates, detect_forward_backward);
    }

    template<typename Network, typename Track>
    std::set<defect>
    matcher<Network, Track>::detect(
            const Track &track, const std::size_t from, const std::size_t to,
            const bool detect_forward_backward) const {
        return _detector.detect(track, from, to, detect_forward_backward);
    }

    template<typename Network, typename Track>
    Track
    matcher<Network, Track>::remove_defects(const Track &track, const std::vector<std::set<defect>> &defects) const {
        std::set<std::size_t> indices_to_remove;
        for (std::size_t i = 0; i < defects.size(); ++i) {
            std::set<defect> defect_set = defects[i];
            if (not defect_set.contains(defect::none)) {
                indices_to_remove.emplace(i);
            }
        }

        return track.thin_out(indices_to_remove);
    }

    template<typename Network, typename Track>
    boost::geometry::model::multi_polygon<boost::geometry::model::polygon<typename matcher<Network, Track>::point_type>>
    matcher<Network, Track>::_create_buffer(const point_type &point, const double buffer_radius,
                                            std::size_t buffer_points) const {
        boost::geometry::model::multi_polygon<boost::geometry::model::polygon<point_type>> buffer;

        const buffer_distance_strategy_type buffer_distance_strategy{buffer_radius};

        std::size_t automatic_buffer_points = geometry::next_pow2((std::uint64_t) (buffer_radius / 4));
        buffer_points = buffer_points == 0 ? automatic_buffer_points : std::max(buffer_points, automatic_buffer_points);

        if constexpr(std::is_same_v<typename boost::geometry::coordinate_system<point_type>::type,
                geometry::cs_geographic>) {
            using buffer_point_strategy_type = boost::geometry::strategy::buffer::geographic_point_circle<>;
            const buffer_point_strategy_type buffer_point_strategy{buffer_points};

            boost::geometry::buffer(point, buffer, buffer_distance_strategy, buffer_side_strategy,
                                    buffer_join_strategy, buffer_end_strategy, buffer_point_strategy);
        } else if constexpr(std::is_same_v<typename boost::geometry::coordinate_system<point_type>::type,
                geometry::cs_cartesian>) {
            using buffer_point_strategy_type = boost::geometry::strategy::buffer::point_circle;
            const buffer_point_strategy_type buffer_point_strategy{buffer_points};

            boost::geometry::buffer(point, buffer, buffer_distance_strategy, buffer_side_strategy,
                                    buffer_join_strategy, buffer_end_strategy, buffer_point_strategy);
        } else {
            static_assert(not std::is_same_v<typename boost::geometry::coordinate_system<point_type>::type,
                    geometry::cs_geographic> and
                          not std::is_same_v<typename boost::geometry::coordinate_system<point_type>::type,
                                  geometry::cs_cartesian>, "Invalid coordinate system");
        }

        return buffer;
    }

    template<typename Network, typename Track>
    std::size_t matcher<Network, Track>::_candidate_search_nearest_measurement(
            std::multiset<candidate_edge_type, candidate_edge_distance_comparator_type> &candidate_edge_set,
            const std::size_t round, const Track &track, const std::size_t index, const std::size_t number,
            const bool with_reverse, const bool with_adjacent) const {
        assert(index < track.measurements.size());
        const measurement_type &measurement = track.measurements[index];

        // query rtree
        auto edge_result = network.query_segments_unique(
                boost::geometry::index::nearest(measurement.point, number));

        if (with_adjacent) {
            for (const auto &edge_descriptor: edge_result) {
                const auto &edge = network.graph[edge_descriptor];
                const auto edge_distance = geometry::distance(measurement.point, edge.line);

                const auto source = boost::source(edge_descriptor, network.graph);
                const auto target = boost::target(edge_descriptor, network.graph);

                for (const auto vertex: std::array{source, target}) {
                    for (const auto out_edge: boost::make_iterator_range(boost::out_edges(vertex, network.graph))) {
                        const auto &adj_edge = network.graph[out_edge];
                        const auto adj_distance = geometry::distance(measurement.point, adj_edge.line);
                        if (adj_distance <= edge_distance) {
                            edge_result.emplace(out_edge);
                        }
                    }
                }
            }
        }

        if (with_reverse) {
            for (const auto edge_descriptor: edge_result) {
                const auto source = boost::source(edge_descriptor, network.graph);
                const auto target = boost::target(edge_descriptor, network.graph);
                for (const auto out_edge: boost::make_iterator_range(boost::out_edges(target, network.graph))) {
                    if (boost::target(out_edge, network.graph) == source) {
                        edge_result.emplace(out_edge);
                    }
                }
            }
        }

        _process_candidate_query(candidate_edge_set, round, track, index, edge_result);

        return number;
    }

    template<typename Network, typename Track>
    double matcher<Network, Track>::_candidate_search_buffer_measurement(
            std::multiset<candidate_edge_type, candidate_edge_distance_comparator_type> &candidate_edge_set,
            const std::size_t round, const Track &track, const std::size_t index, const std::size_t buffer_points,
            const double buffer_radius, const double buffer_upper_radius, const double buffer_lower_radius,
            bool adaptive_radius) const {
        assert(index < track.measurements.size());
        const auto &measurement = track.measurements[index];

        bool found = false;
        std::set<edge_descriptor> edge_result;

        double current_buffer_radius = buffer_radius;
        while (not found) {
            if (adaptive_radius) {
                if (index > 0) {
                    current_buffer_radius =
                            std::min(current_buffer_radius, (double) track.rich_segments[index - 1].length);
                }
                if (index < track.measurements.size() - 1) {
                    current_buffer_radius =
                            std::min(current_buffer_radius, (double) track.rich_segments[index].length);
                }

                if (current_buffer_radius < buffer_lower_radius) {
                    // needs to be at least minimum radius
                    current_buffer_radius = buffer_lower_radius;
                }
            }

            current_buffer_radius = std::min(current_buffer_radius, buffer_upper_radius);

            // create bounding box with at least buffer_distance
            const auto buffer = _create_buffer(measurement.point, current_buffer_radius, buffer_points);

            // query rtree
            edge_result = network.query_segments_unique(boost::geometry::index::intersects(buffer));

            if (edge_result.empty() and current_buffer_radius != buffer_upper_radius) {
                current_buffer_radius *= 2;
                adaptive_radius = false;
            } else {
                found = true;
            }
        }

        _process_candidate_query(candidate_edge_set, round, track, index, edge_result);

        return current_buffer_radius;
    }

    template<typename Network, typename Track>
    void matcher<Network, Track>::_process_candidate_query(
            std::multiset<candidate_edge_type, candidate_edge_distance_comparator_type> &candidate_edge_set,
            const std::size_t round, const Track &track, const std::size_t index,
            std::set<edge_descriptor> &edge_result) const {
        assert(index < track.measurements.size());
        const measurement_type &measurement = track.measurements[index];

        // remove edges from result that were already processes in candidate_edge_set
        for (const auto &candidate_edge: candidate_edge_set) {
            edge_result.erase(candidate_edge.edge_descriptor);
        }

        for (const auto &edge_descriptor: edge_result) {
            const auto &edge = network.graph[edge_descriptor];
            if (edge.has_length) {
                segment_type projection;
                boost::geometry::closest_points(measurement.point, edge.line, projection);
                auto projection_distance = geometry::point_distance(projection.first, projection.second);
                if (projection_distance == geometry::default_float_type<distance_type>::v0) {
                    // when distance is zero, replace closest point with measurement point as it is already fine
                    projection.second = projection.first;
                }

                // check if point is near a segment point first, if so, replace with it
                const auto nearest_segments = network.query_segments(
                        boost::geometry::index::nearest(projection.second, 1));
                const point_type *segment_point = nullptr;
                distance_type segment_point_distance = geometry::default_float_type<distance_type>::v1;
                if (not nearest_segments.empty()) {
                    const auto &nearest_segment = nearest_segments.front().first;
                    segment_point = &nearest_segment.first;
                    segment_point_distance = geometry::point_distance(
                            nearest_segment.first, projection.second);
                    auto tmp_distance = geometry::point_distance(
                            nearest_segment.second, projection.second);
                    if (tmp_distance < segment_point_distance) {
                        segment_point_distance = tmp_distance;
                        segment_point = &nearest_segment.second;
                    }
                }
                if (segment_point != nullptr and not geometry::equals_points(*segment_point, projection.second) and
                    segment_point_distance < 1e-4) {
                    // if within one millimeter, replace point with found point, so "snap to network"
                    projection.second = *segment_point;
                    projection_distance = geometry::point_distance(projection.first, projection.second);
                }

                rich_line_type from_line, to_line;
                if (geometry::equals_points(edge.line.front(), projection.second)) {
                    // projection is equal to front, from is empty, to is edge
                    to_line = edge;
                } else if (geometry::equals_points(edge.line.back(), projection.second)) {
                    // projection is equal to back, from is edge, to is empty
                    from_line = edge;
                } else {
                    // projection is not equal, check distances
                    const auto from_distance = geometry::point_distance(edge.line.front(), projection.second);
                    if (from_distance == geometry::default_float_type<distance_type>::v0) {
                        // from distance is zero, from is empty, to is edge
                        to_line = edge;
                        projection.second = edge.line.front();
                        projection_distance = geometry::point_distance(projection.first, projection.second);
                    } else {
                        const auto to_distance = geometry::point_distance(edge.line.back(), projection.second);
                        if (to_distance == geometry::default_float_type<distance_type>::v0) {
                            // to distance is zero, from is edge, to is empty
                            from_line = edge;
                            projection.second = edge.line.back();
                            projection_distance = geometry::point_distance(projection.first, projection.second);
                        } else {
                            // we need to cut substrings
                            from_line = geometry::substring_point_to<rich_line_type>(edge, projection.second, true);
                            assert(from_line.has_length and
                                   from_line.length > geometry::default_float_type<length_type>::v0);
                            if (from_line.length == edge.length) {
                                // from line has same length as edge, replace with edge again, to_line is empty
                                from_line = edge;
                                projection.second = edge.line.back();
                                projection_distance = geometry::point_distance(projection.first, projection.second);
                            } else {
                                to_line = geometry::substring_point_from<rich_line_type>(edge, projection.second, true);
                                assert(to_line.has_length and
                                       to_line.length > geometry::default_float_type<length_type>::v0);
                                if (to_line.length == edge.length) {
                                    // to line has same length as edge, replace with edge again, replace from to empty
                                    from_line = rich_line_type{};
                                    to_line = edge;
                                    projection.second = edge.line.front();
                                    projection_distance = geometry::point_distance(
                                            projection.first, projection.second);
                                }
                            }
                        }
                    }
                }

                candidate_edge_set.emplace(candidate_edge_type{
                        false, round, edge_descriptor, projection.second, projection_distance, std::move(from_line),
                        std::move(to_line)});
            }
        }
    }

    template<typename Network, typename Track>
    void matcher<Network, Track>::_finalize_candidates(
            std::vector<candidate_type> &candidates, const std::size_t round, const Track &track,
            std::vector<std::multiset<candidate_edge_type, candidate_edge_distance_comparator_type>> &candidate_edge_sets,
            const bool candidate_adoption_siblings, const bool candidate_adoption_nearby,
            const bool candidate_adoption_reverse) const {
        const auto add_candidate = [round](const auto &point, const auto &candidate_edge, auto &edge_set) {
            const auto distance = geometry::point_distance(point, candidate_edge.projection_point);

            candidate_edge_type new_candidate_edge{
                    true, round, candidate_edge.edge_descriptor, candidate_edge.projection_point,
                    distance, candidate_edge.from, candidate_edge.to};

            const auto range = edge_set.equal_range(new_candidate_edge);

            if (range.first == edge_set.cend()) {
                edge_set.emplace(std::move(new_candidate_edge));
            } else if (range.first != edge_set.cend()) {
                bool emplace = true;
                for (auto it = range.first; it != range.second; it++) {
                    if (*it == new_candidate_edge) {
                        emplace = false;
                        break;
                    }
                }

                if (emplace) {
                    edge_set.emplace(std::move(new_candidate_edge));
                }
            }
        };

        if (candidate_adoption_nearby) {
            using index_value_type = std::pair<segment_type,
                    std::pair<std::size_t, std::reference_wrapper<const candidate_edge_type>>>;

            // create candidates index
            std::size_t candidates_number = 0;
            for (const auto &edge_set: candidate_edge_sets) {
                for (const auto &candidate: edge_set) {
                    if (not candidate.adopted) {
                        candidates_number++;
                    }
                }
            }

            std::vector<index_value_type> candidates_index_values;
            candidates_index_values.reserve(candidates_number);
            for (std::size_t index = 0; index < candidate_edge_sets.size(); ++index) {
                const auto &measurement = track.measurements[index];
                auto &edge_set = candidate_edge_sets[index];
                for (const auto &candidate: edge_set) {
                    if (not candidate.adopted) {
                        candidates_index_values.emplace_back(
                                std::pair{segment_type{measurement.point, candidate.projection_point},
                                          std::pair{index, std::cref(candidate)}});
                    }
                }
            }

            boost::geometry::index::rtree<index_value_type, boost::geometry::index::rstar<16>>
                    candidates_index{std::move(candidates_index_values)};

            // search in index for adopting nearby candidates not directly attached
            for (std::size_t index = 0; index < candidate_edge_sets.size(); ++index) {
                const auto &measurement = track.measurements[index];
                auto &edge_set = candidate_edge_sets[index];
                auto edge_set_it = edge_set.crbegin();
                if (edge_set_it != edge_set.crend()) {
                    const auto longest_distance = edge_set_it->distance;

                    const auto buffer = _create_buffer(measurement.point, longest_distance);

                    std::vector<index_value_type> results;
                    candidates_index.query(boost::geometry::index::intersects(buffer), std::back_inserter(results));

                    for (const auto &index_value: results) {
                        const auto &candidate_edge_pair = index_value.second;
                        std::size_t result_index = candidate_edge_pair.first;
                        if (index != result_index) {
                            // skip self references
                            const candidate_edge_type &candidate_edge = candidate_edge_pair.second;
                            add_candidate(measurement.point, candidate_edge, edge_set);

                            if (candidate_adoption_reverse) {
                                auto &reverse_candidate_edge_set = candidate_edge_sets[result_index];
                                for (const auto &edge: edge_set) {
                                    if (not edge.adopted) {
                                        const auto &reverse_measurement = track.measurements[result_index];
                                        add_candidate(reverse_measurement.point, edge, reverse_candidate_edge_set);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        if (candidate_adoption_siblings) {
            // adopt from previous and next candidate set the directly attached candidates
            for (std::size_t index = 0; index < candidate_edge_sets.size(); ++index) {
                const auto &measurement = track.measurements[index];
                auto &edge_set = candidate_edge_sets[index];

                // we have a previous candidate set
                if (index >= 1) {
                    for (const auto &candidate_edge: candidate_edge_sets[index - 1]) {
                        if (not candidate_edge.adopted) {
                            add_candidate(measurement.point, candidate_edge, edge_set);
                        }
                    }
                }

                // we have a next candidate set
                if (index + 1 < candidate_edge_sets.size()) {
                    for (const auto &candidate_edge: candidate_edge_sets[index + 1]) {
                        if (not candidate_edge.adopted) {
                            add_candidate(measurement.point, candidate_edge, edge_set);
                        }
                    }
                }
            }
        }

        // preparing node set and finalizing candidates
        for (std::size_t index = 0; index < candidate_edge_sets.size(); ++index) {
            const auto &measurement = track.measurements[index];
            auto &edge_set = candidate_edge_sets[index];

            // node set
            std::multiset<candidate_node_type, candidate_node_distance_comparator_type> node_set{
                    &candidate_node_type::distance_comparator};
            std::set<typename boost::graph_traits<graph_type>::vertex_descriptor> node_check;
            for (const auto &candidate_edge: edge_set) {
                const auto edge_descriptor = candidate_edge.edge_descriptor;

                const auto source = boost::source(edge_descriptor, network.graph);
                if (not node_check.contains(source)) {
                    const auto &node = network.graph[source];
                    const auto distance = geometry::point_distance(measurement.point, node.point);
                    node_set.emplace(candidate_node_type{source, distance});
                    node_check.emplace(source);
                }

                const auto target = boost::target(edge_descriptor, network.graph);
                if (not node_check.contains(target)) {
                    const auto &node = network.graph[target];
                    const auto distance = geometry::point_distance(measurement.point, node.point);
                    node_set.emplace(candidate_node_type{target, distance});
                    node_check.emplace(target);
                }
            }

            std::vector<candidate_node_type> nodes;
            nodes.reserve(node_set.size());
            std::move(node_set.begin(), node_set.end(), std::back_inserter(nodes));

            std::vector<candidate_edge_type> edges;
            edges.reserve(edge_set.size());
            std::move(edge_set.begin(), edge_set.end(), std::back_inserter(edges));

            if (index >= candidates.size()) {
                // next measurement is equal to current measurement
                bool next_equal = false;
                if (index + 1 < track.measurements.size()) {
                    next_equal = geometry::equals_points(measurement.point, track.measurements[index + 1].point);
                }

                candidates.emplace_back(candidate_type{track, index, next_equal, std::move(nodes), std::move(edges)});
            } else {
                auto &candidate = candidates[index];
                candidate.nodes = std::move(nodes);
                candidate.edges = std::move(edges);
            }
        }
    }

    template<typename Network, typename Track>
    void matcher<Network, Track>::_finalize_candidates_nearest(
            std::vector<candidate_type> &candidates, const std::size_t round, const Track &track,
            const std::vector<std::size_t> &numbers,
            std::vector<std::multiset<candidate_edge_type, candidate_edge_distance_comparator_type>> &candidate_edge_sets,
            const bool candidate_adoption_siblings, const bool candidate_adoption_nearby,
            const bool candidate_adoption_reverse) const {
        _finalize_candidates(
                candidates, round, track, candidate_edge_sets, candidate_adoption_siblings, candidate_adoption_nearby,
                candidate_adoption_reverse);
        assert(candidates.size() == numbers.size());
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            auto &candidate = candidates[i];
            candidate.number = numbers[i];
        }
    }

    template<typename Network, typename Track>
    void matcher<Network, Track>::_finalize_candidates_buffer(
            std::vector<candidate_type> &candidates, const std::size_t round, const Track &track,
            const std::vector<double> &radii,
            std::vector<std::multiset<candidate_edge_type, candidate_edge_distance_comparator_type>> &candidate_edge_sets,
            const bool candidate_adoption_siblings, const bool candidate_adoption_nearby,
            const bool candidate_adoption_reverse) const {
        _finalize_candidates(
                candidates, round, track, candidate_edge_sets, candidate_adoption_siblings, candidate_adoption_nearby,
                candidate_adoption_reverse);
        assert(candidates.size() == radii.size());
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            auto &candidate = candidates[i];
            candidates[i].radius = radii[i];
        }
    }

    template<typename Network, typename Track>
    void matcher<Network, Track>::_prepare_shortest_path_memory() {
        if (not _vertices.get()) {
            _vertices.reset(new std::vector<vertex_descriptor>{});
            _vertices->reserve(boost::num_vertices(network.graph));
        }
        if (not _predecessors.get()) {
            _predecessors.reset(new std::vector<vertex_descriptor>{});
            _predecessors->reserve(boost::num_vertices(network.graph));
            for (const auto vertex: boost::make_iterator_range(boost::vertices(network.graph))) {
                _predecessors->emplace_back(vertex);
            }
        }
        if (not _distances.get()) {
            _distances.reset(new std::vector<length_type>{});
            _distances->reserve(boost::num_vertices(network.graph));
            for (const auto vertex: boost::make_iterator_range(boost::vertices(network.graph))) {
                _distances->emplace_back(std::numeric_limits<length_type>::infinity());
            }
        }
        if (not _colors.get()) {
            _colors.reset(new std::vector<typename boost::default_color_type>{});
            _colors->reserve(boost::num_vertices(network.graph));
            for (const auto vertex: boost::make_iterator_range(boost::vertices(network.graph))) {
                _colors->emplace_back(boost::white_color);
            }
        }
    }

    template
    class matcher<geometry::network::types_geographic::network_static, geometry::track::types_geographic::track_type>;

    template
    class matcher<geometry::network::types_cartesian::network_static, geometry::track::types_cartesian::track_type>;

}
