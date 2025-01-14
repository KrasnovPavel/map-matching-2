// Copyright (C) 2021-2021 Adrian Wöltche
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

#include "hmm.hpp"

#include <geometry/util.hpp>
#include <matching/types.hpp>

namespace map_matching_2::environment {

    template<typename Matcher>
    hmm<Matcher>::hmm(Matcher &matcher, const matching::settings &match_settings)
            : _matcher{matcher}, _match_settings{match_settings} {}

    template<typename Matcher>
    void hmm<Matcher>::set(const track_type &track) {
        if (_match_settings.filter_duplicates or _match_settings.filter_defects) {
            auto defects = _matcher.detect(track, _match_settings.filter_duplicates, _match_settings.filter_defects);
            _track = _matcher.remove_defects(track, defects);
        } else {
            _track = track;
        }

        if (_match_settings.simplify) {
            _track.simplify(false, _match_settings.simplify_distance_tolerance);
        }

        if (_match_settings.median_merge) {
            _track.median_merge(_match_settings.median_merge_distance_tolerance, _match_settings.adaptive_median_merge,
                                &_matcher.network);
        }

        if (_match_settings.k_nearest_candidate_search) {
            _candidates = _matcher.candidate_search_nearest(
                    _track, _match_settings.k_nearest, _match_settings.k_nearest_reverse,
                    _match_settings.k_nearest_adjacent, _match_settings.candidate_adoption_siblings,
                    _match_settings.candidate_adoption_nearby, _match_settings.candidate_adoption_reverse);
        } else {
            _candidates = _matcher.candidate_search_buffer(
                    _track, _match_settings.buffer_points, _match_settings.buffer_radius,
                    _match_settings.buffer_upper_radius, _match_settings.buffer_lower_radius,
                    _match_settings.adaptive_radius, _match_settings.candidate_adoption_siblings,
                    _match_settings.candidate_adoption_nearby, _match_settings.candidate_adoption_reverse);
        }

        // observations
        decltype(_observations) observations;
        _observations.swap(observations);

        _observations.reserve(track.measurements.size());
        for (const auto &candidate: _candidates) {
            double max_distance = 0.0;
            for (const auto &candidate_edge: candidate.edges) {
                if (candidate_edge.distance > max_distance) {
                    max_distance = candidate_edge.distance;
                }
            }

            // fix for dividing through zero and for removing -inf values
            max_distance += 0.01;
            max_distance *= 1.01;

            std::vector<double> candidate_observations;
            candidate_observations.reserve(candidate.edges.size());
            for (const auto &candidate_edge: candidate.edges) {
                // max(0.0, (100 - DISTANCE) / 100)
                double observation_probability =
                        std::max(1e-20, (max_distance - candidate_edge.distance) / max_distance);
                if (candidate.index == 0 or candidate.index + 1 >= _candidates.size()) {
                    // on first and last candidate, try to be closer to point
                    observation_probability = std::pow(observation_probability, 3.0);
                }
                observation_probability = std::log(
                        std::pow(observation_probability, _match_settings.hmm_distance_factor));
                assert(observation_probability > -std::numeric_limits<double>::infinity() and
                       observation_probability <= 0.0);
                candidate_observations.emplace_back(observation_probability);
            }
            _observations.emplace_back(std::move(candidate_observations));
        }

        // transitions
        decltype(_transitions) transitions;
        _transitions.swap(transitions);

        _transitions.reserve(_candidates.size());
        for (std::int64_t from = -1, to = 0; to < _candidates.size(); ++from, ++to) {
            if (from < 0) {
                // initial probabilities
                const auto initial_states = _candidates[to].edges.size();
                std::vector<std::vector<double>> start_probabilities;
                start_probabilities.reserve(initial_states);
                double initial_probability = std::log(1.0 / initial_states);
                for (std::size_t target = 0; target < initial_states; ++target) {
                    start_probabilities.emplace_back(std::vector{initial_probability});
                }
                _transitions.emplace_back(std::move(start_probabilities));
            } else {
                const auto &from_candidate = _candidates[from];
                const auto &to_candidate = _candidates[to];

                std::vector<std::vector<double>> candidate_probabilities;
                candidate_probabilities.reserve(to_candidate.edges.size());
                for (std::size_t target = 0; target < to_candidate.edges.size(); ++target) {
                    std::vector<double> transition_probabilities;
                    transition_probabilities.reserve(from_candidate.edges.size());
                    for (std::size_t source = 0; source < from_candidate.edges.size(); ++source) {
                        const auto route = _matcher.candidate_route(
                                _candidates, from, source, to, target, _match_settings.routing_max_distance_factor);

                        if (route.is_invalid) {
                            // invalid route, not possible
                            transition_probabilities.emplace_back(std::log(0.0));
                        } else {
                            double transition_probability = std::log(1.0);
                            if (from_candidate.next_equal and not route.has_length) {
                                // no distance between candidates and empty route, we stay here
                                transition_probability += std::log(1.0);
                            } else {
                                // calculate transition probability
                                typename Matcher::length_type next_distance =
                                        to_candidate.track->rich_segments[to_candidate.index - 1].length;
                                typename Matcher::length_type route_length =
                                        geometry::default_float_type<typename Matcher::length_type>::v1;
                                if (route.has_length) {
                                    route_length = route.length;
                                }

//                                double length_probability = std::max(1e-20L, std::min(next_distance, route_length) /
//                                                                             std::max(next_distance, route_length));

                                auto route_diff = std::fabs(next_distance - route_length);

                                double length_probability = 1.0 - (route_diff / std::max(next_distance, route_length));
                                length_probability = std::max(1e-20, length_probability);

                                if (route_length > 0.0 and next_distance > 0.0) {
                                    const auto route_factor = route_length / next_distance;
                                    // when route is longer than next distance, it might be bad,
                                    // if it is shorter, it might be good
                                    length_probability *= route_factor > 1.0 ? 1.0 / route_factor : route_factor;

                                    if (route_factor > 10.0) {
                                        // when route is more than 10 times longer than next distance, low probability
                                        length_probability *= 0.01;
                                    }
                                }

                                length_probability = std::log(
                                        std::pow(length_probability, _match_settings.hmm_length_factor));
                                assert(length_probability > -std::numeric_limits<double>::infinity() and
                                       length_probability <= 0.0);
                                transition_probability += length_probability;

                                typename Matcher::coordinate_type next_azimuth;
                                if (route.has_azimuth) {
                                    next_azimuth = to_candidate.track->rich_segments[to_candidate.index - 1].azimuth;
                                    const auto diff_azimuth = geometry::angle_diff(next_azimuth, route.azimuth);
                                    double azimuth_probability =
                                            std::max(1e-20, (180.0 - std::fabs(diff_azimuth)) / 180.0);

                                    azimuth_probability = std::log(
                                            std::pow(azimuth_probability, _match_settings.hmm_azimuth_factor));
                                    assert(azimuth_probability > -std::numeric_limits<double>::infinity() and
                                           azimuth_probability <= 0.0);
                                    transition_probability += azimuth_probability;
                                }

                                if (route.has_directions) {
                                    double absolute_directions_probability =
                                            std::max(1e-20, (360.0 - route.absolute_directions) / 360.0);

                                    absolute_directions_probability = std::log(
                                            std::pow(absolute_directions_probability,
                                                     _match_settings.hmm_direction_factor));
                                    assert(absolute_directions_probability >
                                           -std::numeric_limits<double>::infinity() and
                                           absolute_directions_probability <= 0.0);
                                    transition_probability += absolute_directions_probability;

                                    if (route.has_azimuth) {
                                        const auto diff_azimuth = geometry::angle_diff(
                                                next_azimuth, route.azimuth + route.directions);
                                        double azimuth_probability =
                                                std::max(1e-20, (180.0 - std::fabs(diff_azimuth)) / 180.0);

                                        azimuth_probability = std::log(
                                                std::pow(azimuth_probability, _match_settings.hmm_azimuth_factor));
                                        assert(azimuth_probability > -std::numeric_limits<double>::infinity() and
                                               azimuth_probability <= 0.0);
                                        transition_probability += azimuth_probability;
                                    }
                                }
                            }
                            transition_probabilities.emplace_back(transition_probability);
                        }
                    }
                    candidate_probabilities.emplace_back(std::move(transition_probabilities));
                }
                _transitions.emplace_back(std::move(candidate_probabilities));
            }
        }
    }

    template
    class hmm<matching::types_geographic::matcher_static>;

    template
    class hmm<matching::types_cartesian::matcher_static>;

}

