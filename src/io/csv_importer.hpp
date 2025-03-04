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

#ifndef MAP_MATCHING_2_CSV_IMPORTER_HPP
#define MAP_MATCHING_2_CSV_IMPORTER_HPP

#include <vector>

#include <csv.hpp>

#include "importer.hpp"

namespace map_matching_2::io {

    class csv_importer : public importer {

    private:
        std::vector<std::string> _columns;

    protected:
        virtual std::uint64_t parse_time(const std::string &time_str, const std::string &format);

        virtual void configure_format(csv::CSVFormat &format);

        virtual void process_row(const std::size_t n, const csv::CSVRow &row) = 0;

        virtual void finish_import();

    public:
        explicit csv_importer(std::string filename);

        void read() override;

        [[nodiscard]] const std::vector<std::string> &columns() const;

    };

}

#endif //MAP_MATCHING_2_CSV_IMPORTER_HPP
