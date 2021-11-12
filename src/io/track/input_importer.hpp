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

#ifndef MAP_MATCHING_2_INPUT_IMPORTER_HPP
#define MAP_MATCHING_2_INPUT_IMPORTER_HPP

#include <unordered_map>
#include <boost/tokenizer.hpp>

namespace map_matching_2::io::track {

    template<typename MultiTrack>
    class input_importer {
        boost::char_separator<char> separator{",;|"};

    protected:
        std::unordered_map<std::string, MultiTrack> &_tracks;

    public:
        using measurement_type = typename MultiTrack::measurement_type;
        using point_type = typename measurement_type::point_type;
        using line_type = typename MultiTrack::line_type;

        explicit input_importer(std::unordered_map<std::string, MultiTrack> &tracks);

        void read();

    };

}

#endif //MAP_MATCHING_2_INPUT_IMPORTER_HPP
